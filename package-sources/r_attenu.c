/*****************************************************************************
 ************************** r_attenu.c ***************************************
 *****************************************************************************
 *
 * r_attenu - handle button events and control relay attenuator.
 *            Optionally handle IR remote via LIRC.
 *
 * Original by Allo.com (WiringPi version)
 * Ported to lgpio by fooNerd for Volumio 4.x (Bookworm)
 *
 * Based on irexec by Trent Piepho <xyzzy@u.washington.edu>
 *                    Christoph Bartelmus <lirc@bartelmus.de>
 *
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
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
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

/* Version */
#define R_ATTENU_VERSION "2.0.0"

/* GPIO and I2C configuration */
#define INT_GPIO        5       /* BCM GPIO pin for button interrupt */
#define I2C_BUS         1       /* I2C bus number */
#define SWITCH_ADDR     0x20    /* I2C address for button switch */
#define RELAY_ADDR      0x21    /* I2C address for relay attenuator */

/* Volume configuration */
#define DEFAULT_VOL     0x1f    /* Default volume (0-63) */
#define MAX_VOL         0x3f    /* Maximum volume */

/* Socket and file paths */
#define UNIX_SOCK_PATH  "/tmp/ratt"
#define IRCTL_FILE      "/etc/r_attenu.conf"

#ifndef PACKET_SIZE
#define PACKET_SIZE     256
#endif

void print_usage(char *prog)
{
    printf("Usage: %s [options] [lircrc_config_file]\n"
           "\t-d --daemon\t\tRun in background\n"
           "\t-h --help\t\tDisplay usage summary\n"
           "\t-v --version\t\tDisplay version\n"
           "\t-l --withoutLIRC\tProgram will work without IR control\n"
           "\t-n --name=progname\tUse this program name for lircrc matching\n"
           "\t-c --lircdconfig=file\tLIRCRC config file path\n", prog);
}

static const struct option options[] = {
    { "help",        no_argument,       NULL, 'h' },
    { "version",     no_argument,       NULL, 'v' },
    { "daemon",      no_argument,       NULL, 'd' },
    { "withoutLIRC", no_argument,       NULL, 'l' },
    { "name",        required_argument, NULL, 'n' },
    { "lircdconfig", required_argument, NULL, 'c' },
    { 0,             0,                 0,    0   }
};

/* Global state */
static int opt_daemonize    = 0;
static char *opt_progname   = "r_attenu";
static char *progname       = "r_attenu";
static char *opt_lircconfig = NULL;
static unsigned int vol     = DEFAULT_VOL;
static volatile sig_atomic_t end_program = 0;
static unsigned char mute   = 0x00;
static bool ir_Enable       = true;

/* lgpio handles */
static int gpio_handle      = -1;
static int switch_handle    = -1;
static int relay_handle     = -1;
static int socket_fd        = -1;

#ifndef NO_LIRC
static struct lirc_config *lirc_cfg = NULL;
static int lirc_fd          = -1;
#endif

/* Mutex for thread-safe button handling */
static pthread_mutex_t vol_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Button switch values (active low, directly from I2C) */
#define BTN_MUTE        0xf7    /* Mute button */
#define BTN_VOL_DEC     0xfd    /* Volume down */
#define BTN_VOL_INC     0xfe    /* Volume up */
#define BTN_PLAY_PAUSE  0xfb    /* Play/Pause */

/*
 * Volume persistence functions
 */
static int retrieve_vol(void)
{
    FILE *fp;
    int data = 0;

    if ((fp = fopen(IRCTL_FILE, "r")) == NULL) {
        return DEFAULT_VOL;
    }
    if (fscanf(fp, "%x", &data) != 1) {
        fclose(fp);
        return DEFAULT_VOL;
    }
    fclose(fp);

    if ((data < 0) || (data > MAX_VOL))
        return DEFAULT_VOL;
    return data;
}

static void save_vol(int data)
{
    FILE *fp;

    if ((fp = fopen(IRCTL_FILE, "w")) == NULL) {
        fputs("Cannot save volume\n", stderr);
        return;
    }
    fprintf(fp, "%x", data);
    fclose(fp);
}

/*
 * I2C read/write functions using lgpio
 */
static int ra_read(int handle)
{
    int data;

    data = lgI2cReadByte(handle);
    if (data < 0) {
        fputs("Error: Reading from I2C\n", stderr);
    }
    return data;
}

static void ra_write(int handle, unsigned short data)
{
    /* Write 0x3f first to avoid noise (from original code) */
    lgI2cWriteByte(handle, 0x3f);
    usleep(600);

    if (lgI2cWriteByte(handle, data) < 0) {
        fputs("Error: Writing to I2C\n", stderr);
    }
}

/*
 * Volume control functions
 */
static int ra_set_mute(int data)
{
    pthread_mutex_lock(&vol_mutex);
    if ((data == 0) || (data == 1)) {
        mute = data;
        ra_write(relay_handle, (mute ? 0 : (~vol) | 0x40));
        pthread_mutex_unlock(&vol_mutex);
        return 0;
    }
    pthread_mutex_unlock(&vol_mutex);
    return 1;
}

static int ra_get_mute(void)
{
    return mute;
}

static void ra_mute_toggle(void)
{
    pthread_mutex_lock(&vol_mutex);
    mute = (~mute) & 0x1;
    ra_write(relay_handle, (mute ? (~vol) & 0xbf : (~vol) | 0x40));
    pthread_mutex_unlock(&vol_mutex);
}

static int ra_vol_inc(void)
{
    pthread_mutex_lock(&vol_mutex);
    if (vol >= MAX_VOL) {
        pthread_mutex_unlock(&vol_mutex);
        return 1;
    }
    vol += 1;
    ra_write(relay_handle, ((~vol) | 0x40));
    save_vol(vol);
    mute = 0;
    pthread_mutex_unlock(&vol_mutex);
    return 0;
}

static int ra_vol_dec(void)
{
    pthread_mutex_lock(&vol_mutex);
    if (vol <= 0) {
        pthread_mutex_unlock(&vol_mutex);
        return 1;
    }
    vol -= 1;
    ra_write(relay_handle, ((~vol) | 0x40));
    save_vol(vol);
    mute = 0;
    pthread_mutex_unlock(&vol_mutex);
    return 0;
}

static int ra_vol_set(int data)
{
    pthread_mutex_lock(&vol_mutex);
    if ((data >= 0) && (data <= MAX_VOL)) {
        vol = data;
        ra_write(relay_handle, ((~vol) | 0x40));
        save_vol(vol);
        mute = 0;
        pthread_mutex_unlock(&vol_mutex);
        return 0;
    }
    pthread_mutex_unlock(&vol_mutex);
    return 1;
}

static int ra_vol_get(void)
{
    return vol;
}

/*
 * Hardware button event handler (called from GPIO alert)
 */
static void process_button_event(void)
{
    int sw_status;

    sw_status = ra_read(switch_handle);

    switch (sw_status) {
    case BTN_MUTE:
        ra_mute_toggle();
        break;

    case BTN_VOL_DEC:
        ra_vol_dec();
        break;

    case BTN_VOL_INC:
        ra_vol_inc();
        break;

    case BTN_PLAY_PAUSE:
        /* Play/Pause only functional with LIRC */
        break;

    case -1:
        fputs("Error: Reading button status from I2C\n", stderr);
        break;

    default:
        break;
    }
}

/*
 * GPIO alert callback for button interrupt
 */
static void gpio_alert_callback(int num_alerts, lgGpioAlert_p alerts, void *userdata)
{
    int i;
    (void)userdata;

    for (i = 0; i < num_alerts; i++) {
        if (alerts[i].report.level == 0) {
            /* Falling edge - button pressed */
            process_button_event();
        }
    }
}

/*
 * Unix socket functions
 */
static int open_socket(const char *path)
{
    int sockfd;
    struct sockaddr_un serv_addr;
    socklen_t servlen;

    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("creating socket");
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path, path, sizeof(serv_addr.sun_path) - 1);
    servlen = strlen(serv_addr.sun_path) + sizeof(serv_addr.sun_family);

    unlink(path);
    if (bind(sockfd, (struct sockaddr *)&serv_addr, servlen) < 0) {
        perror("binding socket");
        close(sockfd);
        return -1;
    }
    listen(sockfd, 5);

    return sockfd;
}

/*
 * Process commands from socket clients
 */
static char *process_hw_input(char *data)
{
    char *action, *val;
    int value = 70;
    static char response[32];

    action = strtok(data, "=");
    if ((val = strtok(NULL, " ")) != NULL) {
        value = atoi(val);
    }

    if (!strcmp(action, "SET_VOLUME")) {
        if (ra_vol_set(value) == 1)
            return "FAILURE";
        else
            return "SUCCESS";
    } else if (!strcmp(action, "GET_VOLUME")) {
        snprintf(response, sizeof(response), "%d", ra_vol_get());
        return response;
    } else if (!strcmp(action, "SET_MUTE")) {
        if (ra_set_mute(value) == 1)
            return "FAILURE";
        else
            return "SUCCESS";
    } else if (!strcmp(action, "GET_MUTE")) {
        snprintf(response, sizeof(response), "%d", ra_get_mute());
        return response;
    }

    return "FAILURE";
}

#ifndef NO_LIRC
/*
 * Process IR remote input
 */
static int process_ir_input(const char *code)
{
    if (strstr(code, "KEY_VOLUMEUP")) {
        ra_vol_inc();
        return 0;
    } else if (strstr(code, "KEY_VOLUMEDOWN")) {
        ra_vol_dec();
        return 0;
    } else if (strstr(code, "KEY_MUTE")) {
        ra_mute_toggle();
        return 0;
    }
    return -1;
}
#endif

/*
 * Main input processing loop
 */
static void process_input(void)
{
    fd_set readfds;
    int max_sd, ret, i, valread, sd, w_soc;
    int new_socket;
    int client_socket[30];
    int max_clients = 30;
    struct sockaddr_un address;
    socklen_t addrlen;
    char buffer[1025], *retu;

    for (i = 0; i < max_clients; i++)
        client_socket[i] = 0;

    while (!end_program) {
        FD_ZERO(&readfds);
        FD_SET(socket_fd, &readfds);
        max_sd = socket_fd;

#ifndef NO_LIRC
        if (ir_Enable && lirc_fd > 0) {
            FD_SET(lirc_fd, &readfds);
            if (lirc_fd > max_sd)
                max_sd = lirc_fd;
        }
#endif

        for (i = 0; i < max_clients; i++) {
            sd = client_socket[i];
            if (sd > 0)
                FD_SET(sd, &readfds);
            if (sd > max_sd)
                max_sd = sd;
        }

        ret = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if ((ret < 0) && (errno == EINTR)) {
            if (end_program)
                break;
            continue;
        } else if (ret <= 0) {
            continue;
        }

        /* New socket connection */
        if (FD_ISSET(socket_fd, &readfds)) {
            addrlen = sizeof(address);
            new_socket = accept(socket_fd, (struct sockaddr *)&address, &addrlen);
            if (new_socket >= 0) {
                for (i = 0; i < max_clients; i++) {
                    if (client_socket[i] == 0) {
                        client_socket[i] = new_socket;
                        break;
                    }
                }
            }
        }

#ifndef NO_LIRC
        /* LIRC input */
        if (ir_Enable && lirc_fd > 0 && FD_ISSET(lirc_fd, &readfds)) {
            char *code, *string;
            int r;

            if (lirc_nextcode(&code) == 0 && code != NULL) {
                r = lirc_code2char(lirc_cfg, code, &string);
                while (r == 0 && string != NULL) {
                    if (strcasecmp(string, "hardware_control") == 0) {
                        process_ir_input(code);
                    } else {
                        system(string);
                    }
                    r = lirc_code2char(lirc_cfg, code, &string);
                }
                free(code);
            }
        }
#endif

        /* Client socket data */
        for (i = 0; i < max_clients; i++) {
            sd = client_socket[i];
            if (FD_ISSET(sd, &readfds)) {
                valread = read(sd, buffer, 1024);
                if (valread <= 0) {
                    close(sd);
                    client_socket[i] = 0;
                } else {
                    buffer[valread] = '\0';
                    retu = process_hw_input(buffer);
                    w_soc = write(sd, retu, strlen(retu));
                    if (w_soc < 0)
                        perror("ERROR: writing to socket");
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
    end_program = 1;
}

static void setup_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ctrl_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/*
 * Hardware initialization using lgpio
 */
static int init_hardware(void)
{
    int ret;

    /* Open GPIO chip */
    gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle < 0) {
        fprintf(stderr, "Error: Unable to open GPIO chip: %s\n",
                lguErrorText(gpio_handle));
        return EXIT_FAILURE;
    }

    /* Open I2C for switch (buttons) */
    switch_handle = lgI2cOpen(I2C_BUS, SWITCH_ADDR, 0);
    if (switch_handle < 0) {
        fprintf(stderr, "Error: Unable to open I2C for switch (0x%02x): %s\n",
                SWITCH_ADDR, lguErrorText(switch_handle));
        return EXIT_FAILURE;
    }

    /* Open I2C for relay (attenuator) */
    relay_handle = lgI2cOpen(I2C_BUS, RELAY_ADDR, 0);
    if (relay_handle < 0) {
        fprintf(stderr, "Error: Unable to open I2C for relay (0x%02x): %s\n",
                RELAY_ADDR, lguErrorText(relay_handle));
        return EXIT_FAILURE;
    }

    /* Restore volume from file */
    vol = retrieve_vol();
    ra_vol_set(vol);

    /* Set up GPIO alert for button interrupt */
    ret = lgGpioClaimAlert(gpio_handle, 0, LG_FALLING_EDGE, INT_GPIO, -1);
    if (ret < 0) {
        fprintf(stderr, "Error: Unable to claim GPIO %d for alert: %s\n",
                INT_GPIO, lguErrorText(ret));
        return EXIT_FAILURE;
    }

    ret = lgGpioSetAlertsFunc(gpio_handle, INT_GPIO, gpio_alert_callback, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error: Unable to set GPIO alert callback: %s\n",
                lguErrorText(ret));
        return EXIT_FAILURE;
    }

    /* Open Unix socket for control interface */
    socket_fd = open_socket(UNIX_SOCK_PATH);
    if (socket_fd < 0) {
        return EXIT_FAILURE;
    }

    return 0;
}

/*
 * Cleanup function
 */
static void cleanup(void)
{
    save_vol(vol);

#ifndef NO_LIRC
    if (ir_Enable && lirc_cfg != NULL) {
        lirc_freeconfig(lirc_cfg);
        lirc_deinit();
    }
#endif

    if (socket_fd >= 0) {
        close(socket_fd);
        unlink(UNIX_SOCK_PATH);
    }

    if (relay_handle >= 0)
        lgI2cClose(relay_handle);
    if (switch_handle >= 0)
        lgI2cClose(switch_handle);
    if (gpio_handle >= 0)
        lgGpiochipClose(gpio_handle);
}

int main(int argc, char *argv[])
{
    int c;

    while ((c = getopt_long(argc, argv, "hvdln:c:", options, NULL)) != -1) {
        switch (c) {
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
            puts("Running without LIRC.\n");
            ir_Enable = false;
            break;
        case 'c':
            opt_lircconfig = optarg;
            break;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind < argc - 1) {
        fputs("Too many arguments\n", stderr);
        return EXIT_FAILURE;
    }

    setup_handlers();

    if (init_hardware() != 0) {
        fprintf(stderr, "Failed to initialize hardware\n");
        cleanup();
        return EXIT_FAILURE;
    }

#ifndef NO_LIRC
    if (ir_Enable) {
        lirc_fd = lirc_init(opt_progname, 1);
        if (lirc_fd == -1) {
            fprintf(stderr, "Warning: Failed to initialize LIRC, continuing without IR\n");
            ir_Enable = false;
        } else {
            char *config_file = (optind != argc) ? argv[optind] : opt_lircconfig;
            if (lirc_readconfig(config_file, &lirc_cfg, NULL) != 0) {
                fprintf(stderr, "Warning: Failed to read LIRC config, continuing without IR\n");
                lirc_deinit();
                lirc_fd = -1;
                ir_Enable = false;
            }
        }
    }
#else
    ir_Enable = false;
#endif

    if (opt_daemonize) {
        if (daemon(0, 1) == -1) {
            fprintf(stderr, "%s: can't daemonize\n", progname);
            perror(progname);
            cleanup();
            return EXIT_FAILURE;
        }
    }

    process_input();

    cleanup();
    return 0;
}
