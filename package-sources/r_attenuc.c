/* r_attenuc - client for r_attenu daemon
 *
 * Communicates with r_attenu daemon via Unix socket to control
 * Allo Relay Attenuator volume.
 *
 * Original by Allo.com
 * Minor cleanup by fooNerd for Volumio 4.x
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#define SOCKET_PATH "/tmp/ratt"

void print_usage(char *prog)
{
    printf("Usage: %s [options]\n"
           "\t-h\tDisplay usage summary\n"
           "\t-c\tCommand to execute\n"
           "\tCommands:\n"
           "\t\tGET_VOLUME\t\tGet volume (0-63)\n"
           "\t\tSET_VOLUME=[value]\tSet volume (0-63)\n"
           "\t\tGET_MUTE\t\tGet mute status (0/1)\n"
           "\t\tSET_MUTE=[value]\tSet mute (0=unmute 1=mute)\n",
           prog);
}

void error(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    struct sockaddr_un serv_addr;
    int sockfd;
    socklen_t servlen;
    int opt;
    char buffer[82];
    char *cmd = NULL;

    while ((opt = getopt(argc, argv, "hc:")) != -1) {
        switch (opt) {
        case 'c':
            cmd = optarg;
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    if (cmd == NULL) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (optind < argc) {
        fputs("Too many arguments\n", stderr);
        return EXIT_FAILURE;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path, SOCKET_PATH, sizeof(serv_addr.sun_path) - 1);
    servlen = strlen(serv_addr.sun_path) + sizeof(serv_addr.sun_family);

    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        error("Error: Creating socket");

    if (connect(sockfd, (struct sockaddr *)&serv_addr, servlen) < 0)
        error("Error: Connecting");

    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "%s", cmd);
    if (write(sockfd, buffer, strlen(buffer)) < 0)
        error("Error: Writing to socket");

    memset(buffer, 0, sizeof(buffer));
    if (read(sockfd, buffer, 80) < 0)
        error("Error: Reading from socket");

    printf("%s\n", buffer);
    close(sockfd);

    return EXIT_SUCCESS;
}
