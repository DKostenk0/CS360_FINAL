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
#include <libgen.h>
#include "mftp.h"

// Global vars
int debug = 0;
const char* server = "localhost";

// Attempts to open a file located at file_name
// If it fails it returns -1, otherwise returns fd
int open_file(char *file_name) {
    struct stat area, *s= &area;

    // get stats on file
    if (stat(file_name, s) == 0) {
        // if file is regular, try to open it
        if (S_ISREG(s->st_mode)) {
            int file = open(file_name, O_RDONLY);
            if (file == -1) {
                printf("Open/read local file: %s\n", strerror(errno));
                return -1;
            }
            if(debug) printf("  (Debug) Opened file %s\n", file_name);
            // if file opens, return the fd
            return file;
        // if file is dir or special, error out
        } else if (S_ISDIR(s->st_mode)) {
            printf("Open/read local file: Path is a directory\n");
            return -1;
        } else {
            printf("Open/read local file: Path is a special file\n");
            return -1;
        }
    // if you can't get the stats on the file error out with the errno
    } else {
        printf("Open/read local file: %s\n", strerror(errno));
        return -1;
    }
}

void send_file(int file_fd, int data_fd) {
    char buf[4096];
    int read_count;
    while((read_count = read(file_fd, buf, 4096)) > 0) {
        if(debug) printf("Read %d bytes from local file, writing to server\n", read_count);
        write(data_fd, buf, read_count);
    }
}


// Gets a file from the data_fd and writes it to the filename
// on the current working directory
void get_file(char *file_name, int data_fd) {
    // try to create file and all owner permissions
    int file = open(file_name, O_CREAT | O_EXCL | O_WRONLY, S_IRWXU);
    if (file == -1) {
        printf("Open/creating local file: %s\n", strerror(errno));
        return;
    }

    if(debug) printf("  (Debug) Opened file %s\n", file_name);

    // read bytes 512 at a time and write to the file
    char buf[4096] = {0};
    int read_count;
    while ((read_count = read(data_fd, buf, 4096)) > 0) {
        if (read_count == -1) {
            printf("Error receiving data from server: %s\n", strerror(errno));
            close(file);
            return;
        }
        if(debug) printf("Read %d bytes from server, writing to local file\n", read_count);
        write(file, buf, read_count);
    }
    if(debug) printf("CLOSING FD: %d\n", file);
    close(file);
}

// Reads response from the server, if first character its 'E'
// prints out the error response and returns 0, otherwise returns 1
int read_response(int fd) {
    char resp[256] = {0};
    read(fd, resp, 256);

    // If error, print out the error message
    if(resp[0] == 'E') {
        char *error = &resp[1];
        printf("Error response from server: %s", error);
        return 0;
    } else if (debug) {
        printf("  (Debug) Received success response from server\n");
    }
    return 1;
}

// Executes ls -la | more -20 on the client and returns to stdout
void list_directory_contents() {
    int status;
    // fork
    int fd[2];
    pipe(fd);
    if(debug) printf("  (Debug) Forking and listing directory contents\n");
    int cpid = fork();
    // if parent wait for child
    if (cpid) {
        close(fd[1]);

        wait(&status);

        dup2(fd[0], 0);
        execlp("more", "more", "-20", (char*) 0);
    } else {
        close(fd[0]);
        dup2(fd[1], 1);
        execlp("ls", "ls", "-la", (char*) 0);
    }
}

// Gets input from fd and pipes it into stdin
// also piped into more to show 20 lines at a time
void server_data_show(int fd) {
    int status;
    // fork
    if (debug) printf("  (Debug) Forking and reading data from file descriptor %d\n", fd);
    int cpid = fork();
    // if parent wait
    if (cpid) {
        wait(&status);
    } else {
        // get fd to output to stdin
        dup2(fd, 0);
        // execute more to show 20 lines at a time
        execlp("more", "more", "-20", (char *) NULL);
    }
}

// Request a data connection from the server
// Returns the port # or -1 if failed
int request_data_connection(int fd) {
    // send D request
    if (debug) printf("  (Debug) Requesting data connection\n");
    write(fd, "D\n", 2);

    // read response from control connection
    char buf[512] = {0};
    int read_bytes = read(fd, buf, 512);
    char resp = buf[0];

    if(debug) {
        printf("Data Connection Resp (read_bytes: %d) (%ld): '%s'\n", read_bytes, strlen(buf), buf);
    }

    // char pointer to everything after the response code
    char *arg = &buf[1];

    // if you have an error, print the error and return -1
    if (resp == 'E') {
        printf("Error: %s\n", arg);
        return -1;
    }

    int port = atoi(arg);
    if(debug) printf("  (Debug) Data connection ready to connect on port %d\n", port);
    // otherwsie return the response as an int (Expected to be a 5 digit int)
    return port;
}

// connect to a port
// uses the same server address as initially used
// returns -1 on error, returns socket fd on success
int connect_to_data_connection(int port) {
    // setup
    int socketfd;
    struct addrinfo hints, *actualdata;
    memset(&hints, 0, sizeof(hints));

    // setup hint to look at sockets on internet
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
 
    // try to get connection to server
    char str_port[6] = {0};
    // convert int to str
    sprintf(str_port, "%d", port);
    int err = getaddrinfo(server, str_port, &hints, &actualdata);
    if (err != 0) {
        printf("Error: %s\n", gai_strerror(err));
        return -1;
    }

    // set socket file descriptor
    socketfd = socket(actualdata -> ai_family, actualdata -> ai_socktype, 0);

    // Connect to address/port
    if (connect(socketfd, actualdata -> ai_addr, actualdata -> ai_addrlen) < 0) {
        printf("Error: %s\n", strerror(errno));
        return -1;
    }
    if(debug) printf("Connected to %s:%s\n", server, str_port);

    return socketfd;
}

// Main function that interacts with the control connection
void get_user_input(int control_fd) {
    char buffer[512] = {0};
    int running = 1;
    // while you didn't quit
    while (running) {
        char *command;
        char *argument;
        printf("MFTP> ");
        
        // get user input
        fgets(buffer, 512, stdin);
        // parse spaces out
        command = strtok_r(buffer, " ", &argument);
        // parse newline in argument (not there if you have an argument)
        strtok(command, "\n");

        // if arugment exists
        if (argument != NULL) {
            // parse out spaces up until first char in argument
            while(isspace(*argument)) {
                argument++;
            }
            // remove newline ina rgument
            strtok(argument, "\n");
        }

        if(debug) printf("  (Debug) Command: %s, argument: %s\n", command, argument);


        // exit command
        if (strcmp(command, "exit") == 0) {
            // write to server that you're quiting and exit while loop
            write(control_fd, "Q\n", 2);
            running = 0;

        // Change directory on client
        } else if (strcmp(command, "cd") == 0) {
            if (chdir(argument) == -1) {
                printf("Change directory: %s\n", strerror(errno));
            }

        // Change directory on server
        } else if (strcmp(command, "rcd") == 0) {
            // format command to server to be 'Cpath\n'
            char temp[256] = "C\0";
            strcat(temp, argument);
            strcat(temp, "\n");

            // send to server
            write(control_fd, temp, strlen(temp));
            // read response, error if you need to
            read_response(control_fd);
        
        // List directory content on client
        } else if (strcmp(command, "ls") == 0) {
            int status;
            // fork off thread and have child list directory contents
            // since it messes with stdin file descriptors
            int cpid = fork();
            // have parent wait for output of child to finish
            if (cpid) {
                wait(&status);
            } else {
                list_directory_contents();
                exit(0);
            }

        // List direct content on server
        } else if (strcmp(command, "rls") == 0) {
            // request a data connection and connect to it
            int port = request_data_connection(control_fd);
            int data_fd = connect_to_data_connection(port);
            if (data_fd == -1) {
                close(data_fd);
                printf("Error connecting to port %d\n", port);
                continue;
            }

            // request directory content from server
            write(control_fd, "L\n", 2);

            // show the data that returns
            server_data_show(data_fd);
            // close the data connection
            close(data_fd);
            // check for server response/error
            read_response(control_fd);

        // Get a file from the server and put it on the client working dir
        } else if (strcmp(command, "get") == 0) {
            // Get base filename
            char *file_name = basename(argument);
            // check if file exists
            int exists = access(file_name, F_OK);
            if (exists != -1) {
                // if it does, print a pretty error message
                // and go onto the next command
                // TODO: MAKE IT PRETTY
                printf("ERROR: FILE EXISTS\n");
                continue;
            }

            // Create Data Connection
            int port = request_data_connection(control_fd);
            int data_fd = connect_to_data_connection(port);
            if (data_fd == -1) {
                close(data_fd);
                printf("Error connecting to port %d\n", port);
                continue;
            }

            // Send Get request with format 'Gpath\n'
            char temp[256] = "G\0";
            strcat(temp, argument);
            strcat(temp, "\n");
            write(control_fd, temp, strlen(temp));

            // If not an error response (server FILE exists and you can access)
            if (read_response(control_fd)) {
                // Create and read File
                get_file(file_name, data_fd);
            }

            // close data connection
            close(data_fd);

        // Show a file from the server to the client
        } else if (strcmp(command, "show") == 0) {
            // Create and connect to the data connection
            int port = request_data_connection(control_fd);
            int data_fd = connect_to_data_connection(port);
            if (data_fd == -1) {
                close(data_fd);
                printf("Error connecting to port %d\n", port);
                continue;
            }

            // Send Get request with format 'Gpath\n'
            char temp[256] = "G\0";
            strcat(temp, argument);
            strcat(temp, "\n");
            write(control_fd, temp, strlen(temp));

            // If not an error response (server FILE exists and you can access)
            if (read_response(control_fd)) {
                // pipe data connection output into 'more' and display it
                server_data_show(data_fd);
            }
            // close the data connection
            close(data_fd);

        // Put a file from the client onto the server
        } else if (strcmp(command, "put") == 0) {
            // open file for reading (also checks if file exists and can access it)
            int file_fd = open_file(argument);
            if (file_fd < 0) continue;

            // Create and connect to the data connection
            int port = request_data_connection(control_fd);
            int data_fd = connect_to_data_connection(port);
            if (data_fd == -1) {
                close(data_fd);
                printf("Error connecting to port %d\n", port);
                continue;
            }

            // Send Get request with format 'Gpath\n'
            char temp[256] = "P\0";
            strcat(temp, argument);
            strcat(temp, "\n");
            write(control_fd, temp, strlen(temp));

            // If not an error response (server file doesn't exist yet)
            if (read_response(control_fd)) {
                // pipe data connection output into 'more' and display it
                send_file(file_fd, data_fd);
            }
            // close file and data connection
            close(file_fd);
            close(data_fd);


        // Client command unkown, error
        } else {
            printf("Command '%s' is unkown - ignored\n", command);
        }
    }
}

int main(int argc, char const *argv[]) {
    // get default port
    const char* port = "49999";
    // if no args, error out
    if (argc == 1) {
        printf("Please enter command as follows:\n%s [-d] [-p PORT] [-s SERVER]\nport default: 49999\nserver default: localhost", argv[0]);
        exit(0);
    } else if (argc > 1) {
        // if more than one arg, proccess it
        for (int i = 1; i < argc; i+= 2) {
            if (strcmp(argv[i], "-d") == 0) {
                debug = 1;
                i--;
            } else if (strcmp(argv[i], "-p") == 0) {
                port = argv[i+1];
            } else if (strcmp(argv[i], "-s") == 0) {
                server = argv[i+1];
            }
        }
    }
    if(debug) printf(" (Debug) Debug enabled\n");

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
    // loop throug huser inputs and when your done close the socket
    get_user_input(socketfd);
    close(socketfd);
    return 0;
}