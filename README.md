  Name: 	Dmitriy Kostenko
  Asign.:	Final Project
  Class:	CS 360
  Prof.:	Ben McCammish
  Date:	4/24/2020

  Description:
   This is the final project for CS360, it was designed to highlight all of the things we have learned throughout the class, and puts them all together into one project.
   The project itself consists of a small version of a FTP system, with a server-client system, that allows for a server to be spun up, and for a client to connect to the server and execute basic interaction commands on itself, or the server.

   To build the project, a Makefile has been given to easily compile the project:
  		make mftp - makes the mftp client
  		make mftpserve - makes the mftpserve server
   		make all - makes mftp client and mftpserve server

	To start the server execute the following command (optional -d for debugging and -p followed by port)
		./mftpserve [-d] [-p <port>]
		ex: ./mftpserve -p 5000 -d
	To connect the client to the server (default is localhost:49999)
		./mftp [-d] [-s <serve>] [-p <port>]
		ex: ./mftp -s 192.168.70.21 -p 5000 -d

   There are no executable functions for the server, however to start
   The commands you can execute on the client are:
   		cd <pathname> - change direcotory on the client
   		ls - execute bash command 'ls' on the client
   		rcd <pathname> - change direcotory on the server
   		rls - execute bash command 'ls' on the server
   		show <pathname> - show a file (20 lines at at time) from ther server
   		get <pathname> - get a file from the server and put it on the client
   		put <pathname> - put a file from the client to the server
		exit - closes the connection and exits the program

	There are no executable functions for the server, however to start