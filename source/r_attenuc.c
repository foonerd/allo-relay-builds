/*
 * r_attenuc.c - Relay Attenuator Client
 *
 * Client program for communicating with r_attenu daemon.
 * Sends commands via Unix socket to get/set volume and mute.
 *
 * Based on original Allo RelayAttenuator code.
 * Modified for Volumio 4.x compatibility.
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#define PATH "/tmp/ratt"

void print_usage(char *prog)
{
    printf("Usage: %s [options]\n"
           "\t-h\tDisplay usage summary\n"
           "\t-c\tCommand to execute\n"
           "\tCommands:\n"
           "\t\tGET_VOLUME\t\tGet volume (returns 0-63)\n"
           "\t\tSET_VOLUME=<value>\tSet volume (value = 0 to 63)\n"
           "\t\tGET_MUTE\t\tGet mute status (returns 0 or 1)\n"
           "\t\tSET_MUTE=<value>\tSet mute (value = 0/1, 0=unmute 1=mute)\n",
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
    int sockfd, servlen;
    int opt;
    char buffer[82];
    char *cmd = NULL;

    while ((opt = getopt(argc, argv, "hc:")) != -1)
    {
        switch (opt) {
        case 'c':
            cmd = optarg;
            break;

        case 'h':
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (cmd == NULL)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (optind < argc - 1) {
        fputs("Too many arguments\n", stderr);
        return EXIT_FAILURE;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path, PATH, sizeof(serv_addr.sun_path) - 1);
    servlen = strlen(serv_addr.sun_path) + sizeof(serv_addr.sun_family);

    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        error("Error: Creating socket");

    if (connect(sockfd, (struct sockaddr *)&serv_addr, servlen) < 0)
        error("Error: Connecting");

    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer) - 1, "%s", cmd);
    
    if (write(sockfd, buffer, strlen(buffer)) < 0)
        error("Error: Writing to socket");

    memset(buffer, 0, sizeof(buffer));
    if (read(sockfd, buffer, sizeof(buffer) - 1) < 0)
        error("Error: Reading from socket");

    printf("%s\n", buffer);
    close(sockfd);

    return 0;
}
