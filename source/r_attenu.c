/*
 * r_attenu.c - Relay Attenuator Daemon
 *
 * Daemon for controlling Allo Relay Attenuator via I2C.
 * Handles hardware buttons, IR remote (via LIRC), and client commands.
 *
 * Based on original Allo RelayAttenuator code by Allo.com.
 * Ported from WiringPi to lgpio for Raspberry Pi OS Bookworm compatibility.
 * Modified for Volumio 4.x integration.
 *
 * Hardware:
 *   - I2C bus 1
 *   - Switch input: address 0x20
 *   - Relay output: address 0x21
 *   - GPIO interrupt: BCM pin 5
 *   - Volume range: 0-63 (6-bit)
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <stdbool.h>
#include <pthread.h>
#include <lgpio.h>

#ifndef NO_LIRC
#include <lirc/lirc_client.h>
#endif

/*
 * Constants
 */
#define I2C_BUS         1
#define INT_GPIO        5
#define PACKET_SIZE     256
#define TIMEOUT         3
#define DEFAULT_VOL     0x1f
#define IRCTL_FILE      "/etc/r_attenu.conf"
#define UNIX_SOCK_PATH  "/tmp/ratt"
#define R_ATTENU_VERSION "2.0.0"

/*
 * Command line options
 */
static const struct option options[] = {
    { "help",        no_argument,       NULL, 'h' },
    { "version",     no_argument,       NULL, 'v' },
    { "daemon",      no_argument,       NULL, 'd' },
    { "withoutLIRC", no_argument,       NULL, 'l' },
    { "name",        required_argument, NULL, 'n' },
    { "lircdconfig", required_argument, NULL, 'c' },
    { 0,             0,                 0,    0   }
};

/*
 * Global state
 */
static int opt_daemonize    = 0;
static char *opt_progname   = "r_attenu";
static char *progname       = "r_attenu";
static unsigned int vol     = DEFAULT_VOL;
static volatile int end     = 0;
static unsigned char mute   = 0x00;
static bool ir_Enable       = true;

/* lgpio handles */
static int gpio_handle      = -1;
static int i2c_switch_handle = -1;
static int i2c_relay_handle  = -1;

/* I2C addresses */
static const int switchAddr = 0x20;
static const int relayAddr  = 0x21;

/* Unix socket for client communication */
static int rafd = -1;

#ifndef NO_LIRC
static char *opt_lircdconfig = NULL;
static struct lirc_config *config = NULL;
static int lircfd = -1;
#endif

/*
 * Function prototypes
 */
static void print_usage(char *prog);
static int retriveVol(void);
static void saveVol(int data);
static int ra_read(void);
static void ra_write(unsigned short data);
static int ra_set_mute(int data);
static int ra_get_mute(void);
static void ra_mute(void);
static int ra_vol_inc(void);
static int ra_vol_dec(void);
static int ra_vol_set(int data);
static int ra_vol_get(void);
static char *process_hw_input(char *string);
static int create_unix_socket(void);
static void process_input(void);
static void cleanup(void);
static void setup_handlers(void);
static void process_button_event(void);

#ifndef NO_LIRC
static char *get_config(struct lirc_config *config, char *button);
static int lircrc_config_read(char *argv);
static void process_IR_input(char *code);
#endif

/*
 * Print usage information
 */
static void print_usage(char *prog)
{
    printf("Usage: %s [options] [config_file]\n"
           "\t-d --daemon\t\tRun in background\n"
           "\t-h --help\t\tDisplay usage summary\n"
           "\t-v --version\t\tDisplay version\n"
           "\t-l --withoutLIRC\tRun without IR remote control\n"
           "\t-n --name=progname\tProgram name for lircrc matching\n"
           "\t-c --lircdconfig=file\tLIRCD config file\n", prog);
}

/*
 * Volume persistence
 */
static int retriveVol(void)
{
    FILE *fp;
    int data = 0;

    if ((fp = fopen(IRCTL_FILE, "r")) == NULL)
    {
        return DEFAULT_VOL;
    }
    if (fscanf(fp, "%x", &data) != 1)
    {
        fclose(fp);
        return DEFAULT_VOL;
    }
    fclose(fp);

    if ((data < 0) || (data > 0x3f))
        return DEFAULT_VOL;

    return data;
}

static void saveVol(int data)
{
    FILE *fp;

    if ((fp = fopen(IRCTL_FILE, "w")) == NULL)
    {
        fputs("Cannot save Volume\n", stderr);
        return;
    }
    fprintf(fp, "%x", data);
    fclose(fp);
}

/*
 * I2C communication via lgpio
 */
static int ra_read(void)
{
    int data;

    data = lgI2cReadByte(i2c_switch_handle);
    if (data < 0)
    {
        fprintf(stderr, "Error: Reading from I2C switch: %s\n", lguErrorText(data));
        return -1;
    }

    return data;
}

static void ra_write(unsigned short data)
{
    int ret;

    /* Bug fix: write 0x3f first to avoid noise during relay switching */
    ret = lgI2cWriteByte(i2c_relay_handle, 0x3f);
    if (ret < 0)
    {
        fprintf(stderr, "Error: Writing to I2C relay: %s\n", lguErrorText(ret));
        return;
    }
    usleep(600);

    ret = lgI2cWriteByte(i2c_relay_handle, data);
    if (ret < 0)
    {
        fprintf(stderr, "Error: Writing to I2C relay: %s\n", lguErrorText(ret));
    }
}

/*
 * Mute control
 */
static int ra_set_mute(int data)
{
    if ((data == 0) || (data == 1))
    {
        mute = data;
        ra_write(mute ? 0 : (~vol) | 0x40);
        return 0;
    }
    return 1;
}

static int ra_get_mute(void)
{
    return mute;
}

static void ra_mute(void)
{
    mute = (~mute) & 0x1;
    ra_write(mute ? (~vol) & 0xbf : (~vol) | 0x40);
}

/*
 * Volume control
 */
static int ra_vol_inc(void)
{
    if (vol >= 0x3f)
    {
        return 1;
    }
    vol += 1;
    ra_write((~vol) | 0x40);
    saveVol(vol);
    mute = 0;
    return 0;
}

static int ra_vol_dec(void)
{
    if (vol <= 0)
    {
        return 1;
    }
    vol -= 1;
    ra_write((~vol) | 0x40);
    saveVol(vol);
    mute = 0;
    return 0;
}

static int ra_vol_set(int data)
{
    if ((data >= 0) && (data <= 0x3f))
    {
        vol = data;
        ra_write((~vol) | 0x40);
        saveVol(vol);
        mute = 0;
        return 0;
    }
    return 1;
}

static int ra_vol_get(void)
{
    return vol;
}

/*
 * Process hardware button events
 */
static void process_button_event(void)
{
    int swStatus;

    swStatus = ra_read();
    if (swStatus < 0)
        return;

    switch (swStatus)
    {
    case 0xf7: /* Mute button */
        ra_mute();
        break;

    case 0xfd: /* Volume down */
        ra_vol_dec();
        break;

    case 0xfe: /* Volume up */
        ra_vol_inc();
        break;

    case 0xfb: /* Play/Pause - ignored without LIRC */
        break;

    default:
        break;
    }
}

/*
 * GPIO alert callback for button presses
 * Called from lgpio alert thread
 */
static pthread_mutex_t button_mutex = PTHREAD_MUTEX_INITIALIZER;

static void gpio_alert_callback(int num_alerts, lgGpioAlert_p alerts, void *userdata)
{
    int i;
    (void)userdata;

    for (i = 0; i < num_alerts; i++)
    {
        if (alerts[i].report.gpio == INT_GPIO && alerts[i].report.level == 0)
        {
            /* Falling edge detected - button pressed */
            pthread_mutex_lock(&button_mutex);
            process_button_event();
            pthread_mutex_unlock(&button_mutex);
        }
    }
}

/*
 * Process client commands via Unix socket
 */
static char *process_hw_input(char *string)
{
    static char data[64];
    char *action, *val;
    int value = 0;
    char *str_copy;

    str_copy = strdup(string);
    if (!str_copy)
        return "FAILURE";

    action = strtok(str_copy, "=");
    if ((val = strtok(NULL, " ")) != NULL)
        value = atoi(val);

    if (!strcmp(action, "SET_VOLUME"))
    {
        free(str_copy);
        if (ra_vol_set(value) == 1)
            return "FAILURE";
        else
            return "SUCCESS";
    }
    else if (!strcmp(action, "GET_VOLUME"))
    {
        free(str_copy);
        sprintf(data, "%d", ra_vol_get());
        return data;
    }
    else if (!strcmp(action, "SET_MUTE"))
    {
        free(str_copy);
        if (ra_set_mute(value) == 1)
            return "FAILURE";
        else
            return "SUCCESS";
    }
    else if (!strcmp(action, "GET_MUTE"))
    {
        free(str_copy);
        sprintf(data, "%d", ra_get_mute());
        return data;
    }

    free(str_copy);
    return "FAILURE";
}

/*
 * Create Unix socket for client communication
 */
static int create_unix_socket(void)
{
    struct sockaddr_un addr;
    int fd;

    unlink(UNIX_SOCK_PATH);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("Error creating Unix socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, UNIX_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Error binding Unix socket");
        close(fd);
        return -1;
    }

    if (listen(fd, 5) < 0)
    {
        perror("Error listening on Unix socket");
        close(fd);
        return -1;
    }

    return fd;
}

#ifndef NO_LIRC
/*
 * LIRC support functions
 */
static char *get_config(struct lirc_config *cfg, char *button)
{
    struct lirc_config_entry *scan;
    struct lirc_code *code;
    struct lirc_list *code_config;

    if (!cfg)
        return NULL;

    scan = cfg->next;
    while (scan != NULL)
    {
        code = scan->code;
        code_config = scan->config;
        if (code && code_config && !strcmp(button, code->button))
        {
            return code_config->string;
        }
        scan = scan->next;
    }
    return NULL;
}

static void process_IR_input(char *code)
{
    char *button;

    /* Parse button name from LIRC code */
    /* Format: <code> <repeat> <button> <remote> */
    strtok(code, " ");
    strtok(NULL, " ");
    button = strtok(NULL, " ");

    if (!button)
        return;

    if (!strcmp(button, "KEY_VOLUMEUP"))
    {
        ra_vol_inc();
    }
    else if (!strcmp(button, "KEY_VOLUMEDOWN"))
    {
        ra_vol_dec();
    }
    else if (!strcmp(button, "KEY_MUTE"))
    {
        ra_mute();
    }
}

static int lircrc_config_read(char *argv)
{
    if (lirc_readconfig(argv, &config, NULL) == 0)
    {
        return 0;
    }
    return -1;
}
#endif

/*
 * Main processing loop
 */
static void process_input(void)
{
    fd_set readfds;
    int max_sd, ret, i, valread, sd, new_socket;
    int client_socket[30];
    int max_clients = 30;
    struct sockaddr_un address;
    socklen_t addrlen;
    char buffer[1025];
    char *result;

    for (i = 0; i < max_clients; i++)
        client_socket[i] = 0;

    while (!end)
    {
        FD_ZERO(&readfds);
        FD_SET(rafd, &readfds);
        max_sd = rafd;

#ifndef NO_LIRC
        if (ir_Enable && lircfd > 0)
        {
            FD_SET(lircfd, &readfds);
            if (lircfd > max_sd)
                max_sd = lircfd;
        }
#endif

        for (i = 0; i < max_clients; i++)
        {
            sd = client_socket[i];
            if (sd > 0)
                FD_SET(sd, &readfds);
            if (sd > max_sd)
                max_sd = sd;
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        ret = select(max_sd + 1, &readfds, NULL, NULL, &tv);

        if (ret < 0)
        {
            if (errno == EINTR)
            {
                if (end)
                    break;
                continue;
            }
            perror("select error");
            break;
        }

        if (ret == 0)
        {
            /* Timeout - continue */
            continue;
        }

        /* New client connection */
        if (FD_ISSET(rafd, &readfds))
        {
            addrlen = sizeof(address);
            new_socket = accept(rafd, (struct sockaddr *)&address, &addrlen);
            if (new_socket >= 0)
            {
                for (i = 0; i < max_clients; i++)
                {
                    if (client_socket[i] == 0)
                    {
                        client_socket[i] = new_socket;
                        break;
                    }
                }
            }
        }

#ifndef NO_LIRC
        /* LIRC input */
        if (ir_Enable && lircfd > 0 && FD_ISSET(lircfd, &readfds))
        {
            char *code;
            if (lirc_nextcode(&code) == 0 && code != NULL)
            {
                process_IR_input(code);
                free(code);
            }
        }
#endif

        /* Client data */
        for (i = 0; i < max_clients; i++)
        {
            sd = client_socket[i];
            if (sd > 0 && FD_ISSET(sd, &readfds))
            {
                valread = read(sd, buffer, sizeof(buffer) - 1);
                if (valread <= 0)
                {
                    close(sd);
                    client_socket[i] = 0;
                }
                else
                {
                    buffer[valread] = '\0';
                    result = process_hw_input(buffer);
                    if (write(sd, result, strlen(result)) < 0)
                    {
                        perror("Error writing to client");
                    }
                }
            }
        }
    }
}

/*
 * Signal handlers
 */
static void ctrl_handler(int signum)
{
    (void)signum;
    end = 1;
}

static void setup_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ctrl_handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/*
 * Cleanup resources
 */
static void cleanup(void)
{
    if (rafd >= 0)
    {
        close(rafd);
        unlink(UNIX_SOCK_PATH);
    }

#ifndef NO_LIRC
    if (config)
        lirc_freeconfig(config);
    if (lircfd >= 0)
        lirc_deinit();
#endif

    if (i2c_switch_handle >= 0)
        lgI2cClose(i2c_switch_handle);
    if (i2c_relay_handle >= 0)
        lgI2cClose(i2c_relay_handle);
    if (gpio_handle >= 0)
        lgGpiochipClose(gpio_handle);

    saveVol(vol);
}

/*
 * Initialize hardware
 */
static int init_hardware(void)
{
    int ret;

    /* Open GPIO chip */
    gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle < 0)
    {
        fprintf(stderr, "Error opening GPIO chip: %s\n", lguErrorText(gpio_handle));
        return -1;
    }

    /* Open I2C for switch */
    i2c_switch_handle = lgI2cOpen(I2C_BUS, switchAddr, 0);
    if (i2c_switch_handle < 0)
    {
        fprintf(stderr, "Error opening I2C switch (0x%02x): %s\n",
                switchAddr, lguErrorText(i2c_switch_handle));
        return -1;
    }

    /* Open I2C for relay */
    i2c_relay_handle = lgI2cOpen(I2C_BUS, relayAddr, 0);
    if (i2c_relay_handle < 0)
    {
        fprintf(stderr, "Error opening I2C relay (0x%02x): %s\n",
                relayAddr, lguErrorText(i2c_relay_handle));
        return -1;
    }

    /* Claim GPIO for alerts (falling edge) */
    ret = lgGpioClaimAlert(gpio_handle, 0, LG_FALLING_EDGE, INT_GPIO, -1);
    if (ret < 0)
    {
        fprintf(stderr, "Error claiming GPIO %d for alerts: %s\n",
                INT_GPIO, lguErrorText(ret));
        return -1;
    }

    /* Set GPIO callback */
    ret = lgGpioSetAlertsFunc(gpio_handle, INT_GPIO, gpio_alert_callback, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "Error setting GPIO callback: %s\n", lguErrorText(ret));
        return -1;
    }

    /* Retrieve saved volume and set initial state */
    vol = retriveVol();
    ra_write((~vol) | 0x40);

    return 0;
}

/*
 * Main entry point
 */
int main(int argc, char *argv[])
{
    int c;

    while ((c = getopt_long(argc, argv, "hvdln:c:", options, NULL)) != -1)
    {
        switch (c)
        {
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        case 'v':
            printf("%s %s\n", progname, R_ATTENU_VERSION);
            return EXIT_SUCCESS;
        case 'd':
            opt_daemonize = 1;
            break;
        case 'n':
            opt_progname = optarg;
            break;
        case 'l':
            printf("Running without LIRC.\n");
            ir_Enable = false;
            break;
#ifndef NO_LIRC
        case 'c':
            opt_lircdconfig = optarg;
            break;
#endif
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind < argc - 1)
    {
        fputs("Too many arguments\n", stderr);
        return EXIT_FAILURE;
    }

    setup_handlers();

    /* Initialize hardware */
    if (init_hardware() < 0)
    {
        fprintf(stderr, "Failed to initialize hardware\n");
        cleanup();
        return EXIT_FAILURE;
    }

#ifndef NO_LIRC
    /* Initialize LIRC if enabled */
    if (ir_Enable)
    {
        lircfd = lirc_init(opt_progname, 1);
        if (lircfd < 0)
        {
            fprintf(stderr, "Warning: Failed to initialize LIRC, continuing without IR\n");
            ir_Enable = false;
        }
        else
        {
            if (lircrc_config_read(optind != argc ? argv[optind] : NULL) < 0)
            {
                fprintf(stderr, "Warning: Failed to read lircrc, continuing without IR\n");
                lirc_deinit();
                lircfd = -1;
                ir_Enable = false;
            }
        }
    }
#else
    ir_Enable = false;
#endif

    /* Create Unix socket for client communication */
    rafd = create_unix_socket();
    if (rafd < 0)
    {
        fprintf(stderr, "Failed to create Unix socket\n");
        cleanup();
        return EXIT_FAILURE;
    }

    /* Daemonize if requested */
    if (opt_daemonize)
    {
        if (daemon(0, 0) == -1)
        {
            fprintf(stderr, "Failed to daemonize: %s\n", strerror(errno));
            cleanup();
            return EXIT_FAILURE;
        }
    }

    printf("Relay Attenuator started (volume=%d, mute=%d)\n", vol, mute);

    /* Main processing loop */
    process_input();

    /* Cleanup */
    cleanup();

    return EXIT_SUCCESS;
}
