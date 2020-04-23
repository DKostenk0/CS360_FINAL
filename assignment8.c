#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include <unistd.h>

// Server method
void server() {
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
	servaddr.sin_port = htons(49999);

	// Bind socket to the IP/port
	int err =bind(socketfd, (struct sockaddr*) &servaddr, sizeof(struct sockaddr_in));
	if (err > 0) {
		// print error if it 
		printf("Error: %s\n", strerror(errno));
		exit(errno);
	}

	// listen on the socket
	listen(socketfd, 1);
	int connectedTimes = 0;
	while (1) {
		// set the listen file descriptor to the
		// accepted connection
		listenfd = accept(socketfd, NULL, NULL);
		connectedTimes++;
		if (fork()) {
			// close the fd in one proccess
			close(listenfd);
		} else {
			// get client info
			char client[NI_MAXHOST];
			getnameinfo((struct sockaddr*) &servaddr, sizeof(struct sockaddr_in), client, sizeof(client), NULL, 0,0);
			// prin client name and # of times someone connected
			printf("%s %d\n", client, connectedTimes);

			// format time
			time_t curtime;
			time(&curtime);
			char send[18];
			strncpy(send, ctime(&curtime), 18);
			// write the 18 bytes to the client
			write(listenfd, send, 18);
			// close the fd and exit
			close(listenfd);
			exit(0);
		}
	}
}

// Connects to server on port 49999
void client(const char *connectTo) {
	// setup
	int socketfd;
	struct addrinfo hints, *actualdata;
	memset(&hints, 0, sizeof(hints));

	// setup hint to look at sockets on internet
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;

	// try to get connection to server
	int err = getaddrinfo(connectTo, "49999", &hints, &actualdata);
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
	// wait to receive 18 byets from server
	char buf[19];
	while(read(socketfd, buf, 18) != 18) {
	}
	// set the 19th byte to null termiantor string and print
	buf[18] = '\0';
	printf("%s\n", buf);
}

int main(int argc, char const *argv[]){
	// if client or server not provided, error out
	if (argv[1] == NULL) {
		printf("Invalid input, first argument must be 'server' or 'client'\n");
		return 0;
	}

	if (strcmp(argv[1], "server") == 0) {
		// if server, do server
		server();
	} else if (strcmp(argv[1], "client") == 0) {
			// if client, do client with second arg
		client(argv[2]);
	} else {
		// if anything else, error out
		printf("Invalid input, first argument must be 'server' or 'client'\n");
	}

    return 0;
}