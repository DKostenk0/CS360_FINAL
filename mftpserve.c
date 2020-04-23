#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "mftp.h"

int debug = 0;
int data_fd = -1;

void process_commands(int fd);


void send_error(int fd, char *error) {
	char send[256] = "E";
	strcat(send, error);
	strcat(send, "\n");
	write(fd, send, 256);

}

void change_directory(char *path, int fd) {
	if (chdir(path) == 0) {
		write(fd, "A\n", 2);
		printf("Changed current directory to %s\n", path);
	} else {
		printf("Child %d: cd to %s failed with error: %s\n", getpid(), path, strerror(errno));
		send_error(fd, strerror(errno));
	}
}

void list_directory_content(int fd) {
	int status;
	int cpid = fork();
	if (cpid) {
		wait(&status);
	} else {
		dup2(fd, 1);
		execl("/bin/sh", "sh", "-c", "ls -l", (char *) NULL);
	}
}

void send_file(int fd, char *path) {
	printf("Child %d: Reading file, %s\n", getpid(), path);
	struct stat area, *s= &area;
	if (stat(path, s) == 0) {
		if (S_ISREG(s->st_mode)) {
			int file = open(path, O_RDONLY);
			if (file == -1) {
				send_error(fd, strerror(errno));
				return;
			} else {

			}

			char buf[512];
			while (read(file, buf, 512)) {
				write(fd, buf, 512);
			}
			close(file);
		} else if (S_ISDIR(s->st_mode)) {
			send_error(fd, "Path is a directory");
		} else {
			send_error(fd, "Path is a special file");
		}
	} else {
		send_error(fd, strerror(errno));
	}
}

void receive_file(int controlfd, int fd, char *file_name) {
	int file = open(file_name, O_CREAT | O_EXCL | O_WRONLY, S_IRWXU | S_IRGRP | S_IROTH);
	if (file == -1) {
		send_error(controlfd, strerror(errno));
		return;
	} else {
		printf("SENDING ACCEPT\n");
		write(controlfd, "A\n", 2);
	}
	printf("FILE FD: %d\n", file);
	printf("receiving info from fd %d\n", fd);
	char buf[512];
	int read_count = read(fd, buf, 512);
	while(read_count > 0) {
		printf("READING: '%d'\n", read_count);
		write(file, buf, read_count);
		read_count = read(fd, buf, 512);
	}
	close(file);
}

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
	char send[8] = "A";
	char str_port[6];
	sprintf(str_port, "%d", port);
	strcat(send, str_port);
	strcat(send, "\n");

	if(debug) {
		printf("Created new socket with port %d\n", port);
		printf("Sending to client: '%s' (%ld)\n", send, strlen(send));
	}

	write(fd, send, 7);

	listen(socketfd, 1);

	return accept(socketfd, NULL, NULL);
}

void get_argument(int fd, char *arg_loc) {
	read(fd, arg_loc, 256);
	strtok(arg_loc, "\n");
}

void process_commands(int fd) {
	char buf[1];
	while(read(fd, buf, 1)) {
		char command = buf[0];
		if (debug) printf("Child %d: RECEIVED: '%s'\n", getpid(), buf);
		if (command == 'D') {
			data_fd = create_new_socket(fd);

		} else if (command == 'C') {
			char argument[256];
			get_argument(fd, &argument[0]);
			printf("ARGUMENT READ: '%s'\n", argument);
			change_directory(argument, fd);

		} else if (command == 'L') {
			write(fd, "A\n", 2);
			list_directory_content(data_fd);
			close(data_fd);
			data_fd = -1;

		} else if (command == 'G') {
			write(fd, "A\n", 2);
			char argument[256];
			get_argument(fd, &argument[0]);
			send_file(data_fd, argument);
			close(data_fd);
			data_fd = -1;

		}  else if (command == 'P') {
			char argument[256];
			get_argument(fd, &argument[0]);
			receive_file(fd, data_fd, argument);
			close(data_fd);
			data_fd = -1;

		} else if (command == 'Q') {
			printf("Child %d: Quitting\n", getpid());
			break;
		}
		// Clear buffer for next command
		memset(buf,0,strlen(buf));
	}
}

int main(int argc, char const *argv[]) {
	int port = 49999;
	printf("ARGC: %d\n", argc);
	if (argc == 3) {
		if (strcmp(argv[1], "-d") == 0) {
			debug = 1;
			port = atoi(argv[2]);
		} else {
			port = atoi(argv[1]);
		}
	}
	printf("server (%d)\n", port);
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

	// listen on the socket
	listen(socketfd, 4);
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
			char ip_str[INET_ADDRSTRLEN];
			struct in_addr ipAddr = clientaddr.sin_addr;
			if(inet_ntop( AF_INET, &ipAddr, ip_str, INET_ADDRSTRLEN) == NULL) {
				printf("Child %d: %s\n", getpid(), strerror(errno));
			} else {
				printf("Child %d: Client IP address -> %s\n", getpid(), ip_str);
			}

			char client[NI_MAXHOST];
			int err = getnameinfo((struct sockaddr*) &servaddr, sizeof(struct sockaddr_in), client, sizeof(client), NULL, 0, 0);
			if (err != 0) {
				printf("Child %d: %s", getpid(), gai_strerror(err));
			} else {
				printf("Child %d: Connection accepted from host %s\n", getpid(), client);
			}
			process_commands(listenfd);
			exit(0);
		}
	}
	return 0;
}