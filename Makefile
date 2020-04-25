all:
	make mftp
	make mftpserve
mftp: mftp.o mftp.h
	gcc -o mftp ./mftp.o

mftp.o: mftp.c mftp.h
	gcc -c -o mftp.o ./mftp.c

mftpserve: mftpserve.o mftp.h
	gcc -o mftpserve ./mftpserve.o

mftpserve.o: mftpserve.c mftp.h
	gcc -c -o mftpserve.o ./mftpserve.c

clean:
	rm -f mftp.o mftp mftpserve mftpserve.o