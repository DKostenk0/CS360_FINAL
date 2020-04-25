#include <arpa/inet.h>
#include "mftp.h"

#define queue 4 

int debug = 0;
int data_fd = -1;

void process_commands(int fd);


// Sends an error back to the client
void send_error(int fd, char *error) {
	// start with E
	char send[256] = "E";
	// concat it with the error
	strcat(send, error);
	// append a newline
	strcat(send, "\n");
	// send the error
	if(debug) printf("(Debug) Child %d: Sending error to client: '%s'\n", getpid(), send);
	write(fd, send, strlen(send));
}

// Change server directory
void change_directory(char *path, int fd) {
	if (chdir(path) == 0) {
		// send success message if changed correctly
		write(fd, "A\n", 2);
		printf("Changed current directory to %s\n", path);
	} else {
		// Send error and print to server error
		// if something went wrong
		printf("Child %d: cd to %s failed with error: %s\n", getpid(), path, strerror(errno));
		send_error(fd, strerror(errno));
	}
}

// Executes 'ls -la' and redirects output to the given fd
// parent waits for child to execute before returning
void list_directory_content(int fd) {
	int status;
	int cpid = fork();
	if(debug) printf("(Debug) Child %d: Forked proccess %d", getpid(), cpid);
	// have parent wait
	if (cpid) {
		wait(&status);
	} else {
		if(debug) printf("(Debug) Child %d: Printing contents of 'ls -la' to %d\n", getpid(), fd);
		// redirect stdout to fd
		dup2(fd, 1);
		// execute ls -la
		execlp("ls", "ls", "-la", (char *) 0);
	}
}

// sends the file in path to the data_fd
// if it errors out it will report the error to the control fd
void send_file(int control_fd, int data_fd, char *path) {
	printf("Child %d: Reading file, %s\n", getpid(), path);
	struct stat area, *s= &area;
	// Get stats on file
	if (stat(path, s) == 0) {
		// if file is regular, send it over
		if (S_ISREG(s->st_mode)) {
			int file = open(path, O_RDONLY);
			if (file == -1) {
				if(debug) printf("(Debug) Child %d: Error opening file -> %s\n", getpid(), strerror(errno));
				send_error(control_fd, strerror(errno));
				return;
			} else {

			}
			write(control_fd, "A\n", 2);
			printf("Child %d: transmitting file %s to client\n", getpid(), path);
			char buf[4096];
			int write_count;
			// while you can read bytes from the file, write it to the data connection
			while ((write_count = read(file, buf, 4096)) > 0) {
				if (write_count == -1) {
					printf("Child %d: Error transmitting data to client: %s\n", getpid(), strerror(errno));
					close(file);
					return;
				}
				if (debug) printf("Writing %d bytes to data connection\n", write_count);
				write(data_fd, buf, write_count);
			}
			close(file);
			return;
		// if file is dir or special, don't sent it over
		} else if (S_ISDIR(s->st_mode)) {
			if(debug) printf("(Debug) Child %d: Error opening file -> File is a directory\n", getpid());
			send_error(control_fd, "File is a directory");
			return;
		} else {
			if(debug) printf("(Debug) Child %d: Error opening file -> File is a special file\n", getpid());
			send_error(control_fd, "File is a special file");
			return;
		}
	} else {
		if(debug) printf("(Debug) Child %d: Error opening file -> %s\n", getpid(), strerror(errno));
		send_error(control_fd, strerror(errno));
		return;
	}
}

// Receive a file from the data connection
void receive_file(int control_fd, int data_fd, char *file_name) {
	// try to create file in readonly
	if(debug) printf("(Debug) Child %d: Creating file %s\n", getpid(), file_name);
	int file = open(file_name, O_CREAT | O_EXCL | O_WRONLY, S_IRWXU | S_IRGRP | S_IROTH);
	if (file == -1) {
		if(debug) printf("(Debug) Child %d: Error creating file -> %s\n", getpid(), strerror(errno));
		// if you fail, return and send error to client
		send_error(control_fd, strerror(errno));
		return;
	}
	// let client know to start sending data
	write(control_fd, "A\n", 2);
	printf("Child %d: receiving file %s from client\n", getpid(), file_name);
	char buf[4096];
	int read_count;
	// while you are getting data from client, write it to the file
	while((read_count = read(data_fd, buf, 4096)) > 0) {
		if (debug) printf("Writing %d bytes to data connection\n", read_count);
		write(file, buf, read_count);
	}
	close(file);
}
	

// Create a new socket for data transfer
// sends error back if something comes up with creating a socket
// returns the data connection fd when the connection is accepted
int create_new_socket(int fd) {
	struct sockaddr_in servaddr;
	int socketfd;
	// create socket
	socketfd = socket(AF_INET, SOCK_STREAM, 0);

	// set the socket to reuse the port in case of disconnect
	setsockopt(socketfd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));

	// clean servaddr
	memset(&servaddr, 0, sizeof(struct sockaddr_in));

	// set the IP and port
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	// set port to 0 to setup automatic port
	servaddr.sin_port = 0;

	// Bind socket to the IP/port
	int err =bind(socketfd, (struct sockaddr*) &servaddr, sizeof(struct sockaddr_in));
	if (err > 0) {
		send_error(fd,  strerror(errno));
		return -1;
	}

	// Get Port info
	struct sockaddr_in newserver_addr;
	memset(&newserver_addr, 0, sizeof(struct sockaddr_in));
	socklen_t len = sizeof(newserver_addr);
	if(getsockname(socketfd, (struct sockaddr*) &newserver_addr, &len) == -1) {
		send_error(fd,  strerror(errno));
		return -1;
	}

	int port = ntohs(newserver_addr.sin_port);

	// return success with port #
	char send[8] = "A";
	char str_port[6];
	sprintf(str_port, "%d", port);
	strcat(send, str_port);
	strcat(send, "\n");

	if(debug) {
		printf("Created new socket with port %d\n", port);
		printf("Sending to client: '%s' (%ld)\n", send, strlen(send));
	}

	write(fd, send, strlen(send));

	listen(socketfd, 1);

	return accept(socketfd, NULL, NULL);
}

// Simple function that will read the argument sent and strip out the newline
// inserts argument into arg_loc
void get_argument(int fd, char *arg_loc) {
	read(fd, arg_loc, 256);
	strtok(arg_loc, "\n");
}

void process_commands(int fd) {
	// read in first character (it should always be a command)
	char buf[1];
	while(read(fd, buf, 1)) {
		char command = buf[0];
		if (debug) printf("Child %d: Received: '%s'\n", getpid(), buf);

		// Create new data socket
		if (command == 'D') {
			data_fd = create_new_socket(fd);

		// Change Server Directory
		} else if (command == 'C') {
			char argument[256];
			get_argument(fd, &argument[0]);
			change_directory(argument, fd);

		// List server CWD contents
		} else if (command == 'L') {
			write(fd, "A\n", 2);
			list_directory_content(data_fd);
			close(data_fd);
			data_fd = -1;

		// Gets a file, if it doesn't exist, throws an error
		} else if (command == 'G') {
			char argument[256];
			get_argument(fd, &argument[0]);
			send_file(fd, data_fd, argument);
			// close data socket
			close(data_fd);
			data_fd = -1;

		// Put a file from client to server
		}  else if (command == 'P') {
			char argument[256];
			// get filename
			get_argument(fd, &argument[0]);
			// receive file and handle error if file already exists

			receive_file(fd, data_fd, argument);

			// close data socket
			close(data_fd);
			data_fd = -1;

		// Quit, break out of look and go back to main, where child dies
		} else if (command == 'Q') {
			printf("Child %d: Quitting\n", getpid());
			break;
		}

		// Clear buffer for next command
		memset(buf,0,strlen(buf));
	}
}

int main(int argc, char const *argv[]) {
	// setup default port
	int port = 49999;
	if (argc > 1) {
		// process cmd args
        for (int i = 1; i < argc; i+= 2) {
            if (strcmp(argv[i], "-d") == 0) {
                debug = 1;
                i--;
            } else if (strcmp(argv[i], "-p") == 0) {
            	if (argv[i+1] == NULL) {
                    printf("Error: Must have a port after the '%s' argument\n", argv[i]);
                    return 0;
                }
            	port = atoi(argv[i+1]);
            }
        }
	}

	if(debug) printf("(Debug)Parent: Debug output enabled.\n");

	// setup for socket
	struct sockaddr_in servaddr;
	int socketfd, listenfd;

	// create socket
	socketfd = socket(AF_INET, SOCK_STREAM, 0);

	// set the socket to reuse the port in case of disconnect
	setsockopt(socketfd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));

	// clean servaddr
	memset(&servaddr, 0, sizeof(struct sockaddr_in));

	// set the IP and port
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(port);

	// Bind socket to the IP/port
	int err =bind(socketfd, (struct sockaddr*) &servaddr, sizeof(struct sockaddr_in));
	if (err > 0) {
		// print error if it 
		printf("Error: %s\n", strerror(errno));
		exit(errno);
	}
	if(debug) printf("(Debug) Parent: socket created with descriptor %d\n", socketfd);
	if(debug) printf("(Debug) Parent: socket bound to port %d\n", port);
	// listen on the socket
	listen(socketfd, queue);
	if(debug) printf("(Debug) Parent: listening with connection queue of %d\n", queue);
	while (1) {
		// set the listen file descriptor to the
		// accepted connection
		struct sockaddr_in clientaddr;
		socklen_t len = sizeof(clientaddr);
		listenfd = accept(socketfd, (struct sockaddr*) &clientaddr, &len);
		if (fork()) {
			// close the fd in one proccess
			close(listenfd);
		} else {
			// get IP Address
			if (debug) {
				printf("Child %d: started\n", getpid());
				char ip_str[INET_ADDRSTRLEN];
				struct in_addr ipAddr = clientaddr.sin_addr;
				if(inet_ntop( AF_INET, &ipAddr, ip_str, INET_ADDRSTRLEN) == NULL) {
					printf("Child %d: %s\n", getpid(), strerror(errno));
				} else {
					printf("Child %d: Client IP address -> %s\n", getpid(), ip_str);
				}
			}

			// Get Name
			char client[NI_MAXHOST];
			int err = getnameinfo((struct sockaddr*) &servaddr, sizeof(struct sockaddr_in), client, sizeof(client), NULL, 0, 0);
			if (err != 0) {
				printf("Child %d: %s", getpid(), gai_strerror(err));
			} else {
				printf("Child %d: Connection accepted from host %s\n", getpid(), client);
			}
			// Process user commands
			process_commands(listenfd);
			exit(0);
		}
	}
	return 0;
}