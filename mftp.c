#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "mftp.h"

// FOR GET = S_IRWXU

int debug = 0;
const char* port = "49999";
const char* server;

void read_response(int fd) {
    char resp[256];
    read(fd, resp, 256);
    if(debug) printf("RESP (%ld): '%s'\n", strlen(resp), resp);
    if(resp[0] == 'E') {
        char *error = &resp[1];
        printf("Error response from server: %s\n", error);

    }
}

void list_directory_contents() {
    int status;
    int cpid = fork();
    if (cpid) {
        wait(&status);
    } else {
        execl("/bin/sh", "sh", "-c", "ls -l | more -20", (char *) NULL);
    }
}

void server_data_show(int fd) {
    int status;
    int cpid = fork();
    if (cpid) {
        wait(&status);
    } else {
        dup2(fd, 0);
        execl("/bin/sh", "sh", "-c", "more -20", (char *) NULL);
    }
}

int request_data_connection(int fd) {
    write(fd, "D\n", 2);

    char buf[512];
    read(fd, buf, 512);
    char resp = buf[0];

    if(debug) {
        printf("Data Connection Resp: '%s'\n", buf);
    }

    char *arg = &buf[1];

    if (resp == 'E') {
        printf("Error: %s\n", arg);
        return -1;
    }
    return atoi(arg);
}

int connect_to_data_connection(int port) {
    // setup
    int socketfd;
    struct addrinfo hints, *actualdata;
    memset(&hints, 0, sizeof(hints));

    // setup hint to look at sockets on internet
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
 
    // try to get connection to server
    char str_port[6];
    sprintf(str_port, "%d", port);
    int err = getaddrinfo(server, str_port, &hints, &actualdata);
    if (err != 0) {
        printf("Error: %s\n", gai_strerror(err));
        exit(err);
    }

    // set socket file descriptor
    socketfd = socket(actualdata -> ai_family, actualdata -> ai_socktype, 0);

    // Connect to address/port
    if (connect(socketfd, actualdata -> ai_addr, actualdata -> ai_addrlen) < 0) {
        printf("Error: %s\n", strerror(errno));
        exit(errno);
    }
    return socketfd;
}

void get_user_input(int fd) {
    char buffer[512];
    int running = 1;
    while (running) {
        char *command;
        char *argument;
        printf("MFTP> ");
        
        fgets(buffer, 512, stdin);
        command = strtok_r(buffer, " ", &argument);
        strtok(command, "\n");
        if (argument != NULL) {
            while(isspace(*argument)) {
                argument++;
            }
            strtok(argument, "\n");
        }

        if (strcmp(command, "exit") == 0) {
            write(fd, "Q\n", 2);
            running = 0;

        } else if (strcmp(command, "cd") == 0) {
            chdir(argument);

        } else if (strcmp(command, "rcd") == 0) {
            char temp[256] = "C\0";
            strcat(temp, argument);
            strcat(temp, "\n");
            if(debug) printf("SENT: '%s' (%ld)\n", temp, strlen(temp));
            write(fd, temp, strlen(temp));
            read_response(fd);
            
        } else if (strcmp(command, "ls") == 0) {
            list_directory_contents();

        } else if (strcmp(command, "rls") == 0) {
            int port = request_data_connection(fd);
            int data_fd = connect_to_data_connection(port);
            write(fd, "L\n", 2);
            server_data_show(data_fd);
            close(data_fd);
            read_response(fd);

        } else if (strcmp(command, "get") == 0) {
            printf("GET\n");
            
        } else if (strcmp(command, "show") == 0) {
            int port = request_data_connection(fd);
            int data_fd = connect_to_data_connection(port);
            char temp[256] = "G\0";
            strcat(temp, argument);
            strcat(temp, "\n");
            write(fd, temp, strlen(temp));
            server_data_show(data_fd);
            close(data_fd);
            read_response(fd);

        } else if (strcmp(command, "put") == 0) {
            printf("PUT\n");
            
        } else {
            printf("Command '%s' is unkown - ignored\n", command);
        }
    }
}

int main(int argc, char const *argv[]) {
    printf("ARGC: %d\n", argc);
    if (argc == 1) {
        printf("Please enter server address as argument\n");
        exit(0);
    } else if (argc == 3) {
        if (strcmp(argv[1], "-d") == 0) {
            debug = 1;
            server = argv[2];
        }
    } else if (argc == 4) {
        if (strcmp(argv[1], "-d") == 0) {
            debug = 1;
            server = argv[2];
            port = argv[3];
        } else {
            server = argv[1];
            port = argv[2];
        }
    }
    printf("client (%s:%s)\n", server, port);

    // setup
    int socketfd;
    struct addrinfo hints, *actualdata;
    memset(&hints, 0, sizeof(hints));

    // setup hint to look at sockets on internet
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
 
    // try to get connection to server
    int err = getaddrinfo(server, port, &hints, &actualdata);
    if (err != 0) {
        printf("Error: %s\n", gai_strerror(err));
        exit(err);
    }

    // set socket file descriptor
    socketfd = socket(actualdata -> ai_family, actualdata -> ai_socktype, 0);

    // Connect to address/port
    if (connect(socketfd, actualdata -> ai_addr, actualdata -> ai_addrlen) < 0) {
        printf("Error: %s\n", strerror(errno));
        exit(errno);
    }

    printf("Connected to server %s\n", server);
    get_user_input(socketfd);
    close(socketfd);
    return 0;
}