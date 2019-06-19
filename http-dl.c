/*
Copyright 2016 Andrew Hodel
	andrewhodel@gmail.com

LICENSE

MIT

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
 
#define BUFFER_SIZE 1
int fd;
char buffer[BUFFER_SIZE];
unsigned int r = 0;
unsigned int rr = 0;
unsigned int f = 0;
char proto[10];
char hostname[200];
char port[20];
char request[1000];
char filename[200];
char requestString[1000];
int portNum = 0;
int next;
FILE *file;
long long unsigned filesize = 0;
char filesizeString[30];
unsigned long long totalDownloaded = 0;
int canRange = 0;
char requestUrl[1200];

int socket_connect(char *host, in_port_t port){
	struct hostent *hp;
	struct sockaddr_in addr;
	int on = 1, sock;     

	struct timeval tv;

	tv.tv_sec = 10;  /* 10 Secs Timeout */
	tv.tv_usec = 0;  // Not init'ing this can cause strange errors

	if((hp = gethostbyname(host)) == NULL){
		herror("gethostbyname");
		return -1;
	}

	bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(int));
	// set the rcv timeout to 10 seconds
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(struct timeval));

	if(sock == -1){
		perror("setsockopt");
		return -1;
	}
	
	if(connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) == -1){
		perror("connect");
		return -1;
	}
	return sock;
}

void makeRequest() {

	char header[500];
	int throughHeaders = 0;
	int totalHeaders = 0;
	char ch;
	int nl = 0;
	r = 0;
	long long int contentLength = -2;
	long long int currentChunk = 0;
	unsigned int lastDisplayLen = 0;
	char displayString[50];

	unsigned int oneRateTs = (unsigned)time(NULL);
	char oneRate[20];
	unsigned long oneRateB = 0;

	unsigned int fiveRateTs = (unsigned)time(NULL);
	char fiveRate[20];
	unsigned long fiveRateB = 0;

	// get proto
	while (1) {
		proto[r] = requestUrl[r];
		if (requestUrl[r] == ':') {
			break;
		}
		r++;
	}
	proto[r] = '\0';
	// skip //
	r++;
	r++;
	r++;

	if (strcmp(proto, "http") != 0) {
		printf("Protocol not yet supported\n");
		exit(EXIT_FAILURE);
	}

	// get hostname
	rr = 0;
	while (1) {
		hostname[rr] = requestUrl[r];
		if (requestUrl[r] == ':') {
			next = 1;
			break;
		} else if (requestUrl[r] == '/') {
			next = 0;
			break;
		} else if (r >= strlen(requestUrl)) {
			next = 2;
			break;
		}
		r++;
		rr++;
	}
	hostname[rr] = '\0';

	rr = 0;
	if (next == 1) {
		// the previous loop ended at the port delimiter
		// skip the : character
		r++;
		while (1) {
			// add the port chars to port
			port[rr] = requestUrl[r];
			if (requestUrl[r] == '/') {
				next = 0;
				break;
			} else if (r >= strlen(requestUrl)) {
				next = 2;
				break;
			}
			r++;
			rr++;
		}
		port[rr] = '\0';
		portNum = atoi(port);
	} else {
		portNum = 80;
	}

	if (next == 0) {
		rr = 0;
		// the previous loop ended at the / after host or port
		// add remaining chars to request
		while (1) {
			request[rr] = requestUrl[r];
			if (request[rr] == '/') {
				f = 0;
			} else {
				filename[f] = request[rr];
				f++;
			}
			if (r >= strlen(requestUrl)) {
				break;
			}
			r++;
			rr++;
		}
		request[rr] = '\0';
	} else if (next == 2) {
		// there was no trailing /, add it to request
		request[0] = '/';
		request[1] = '\0';
	}
	filename[f] = '\0';

	printf("Requesting http://%s:%i%s\n", hostname, portNum, request);
	printf("filename %s\n", filename);

	if (strlen(filename) == 0) {
		printf("no filename!\n");
		exit(EXIT_FAILURE);
	}

	strcpy(oneRate, "LOADING 1s RATE");
	strcpy(fiveRate, "LOADING 5s RATE");

 	file = fopen(filename, "rb+");

	// check if filename already exists, if it does then restart the dl at the byte position
	if (file == NULL) {
		// the file doesn't exist, create it
		file = fopen(filename, "wb");
		printf("Creating file %s\n", filename);
	} else {
		// the file exists, get length
		fseeko(file, 0, SEEK_END);
		filesize = ftello(file);
		printf("%llu bytes of file already downloaded\n", filesize);
	}

	// generate the request string
	strcpy(requestString, "GET ");
	strcat(requestString, request);
	strcat(requestString, " HTTP/1.1\r\n");
	// if filesize > 0 then send Range header
	if (filesize > 0) {
		strcat(requestString, "Range: bytes=");
		sprintf(filesizeString, "%llu", filesize);
		strcat(requestString, filesizeString);
		strcat(requestString, "-\r\n");
	}
	strcat(requestString, "Host: ");
	strcat(requestString, hostname);
	strcat(requestString, "\r\n");
	strcat(requestString, "Connection: keep-alive\r\n");
	strcat(requestString, "Accept: */*\r\n");
	strcat(requestString, "Accept-Encoding: identity\r\n");
	strcat(requestString, "User-Agent: http-dl.c (linux-gnu)\r\n");
	// don't forget the extra LRCF
	strcat(requestString, "\r\n");

	//printf("requestString:\n%s\n", requestString);
	printf("Sending Request Headers\n");

	fd = socket_connect(hostname, portNum);

	if (fd == -1) {
		printf("socket_connect error, retrying\n");
		fclose(file);
		makeRequest();
	}

	write(fd, requestString, strlen(requestString)); // write(fd, char[]*, len);  
	bzero(buffer, BUFFER_SIZE);

	totalDownloaded = 0;

	// hide cursor
	//fputs("\e[" "?25l", stdout);

	// if read == -1 there was an error,
	// if read == 0 it is end of data
	// if read > 0 it is the length of data read from the recv buffer
	// a socket timeout will return -1 with a errno
	// https://www.gnu.org/software/libc/manual/html_node/I_002fO-Primitives.html
	while ((read(fd, buffer, BUFFER_SIZE)) > 0) {

		ch = buffer[0];

		if (ch == 13 || ch == 10) {
			//printf("%u ", ch);
		} else {
			//printf("%u:%c ", ch, ch);
		}

		if (throughHeaders == 0) {

			header[r] = ch;

			if (ch == 13 && nl == 0) {
				// look for the next \n
				nl++;
			} else if (nl == 1) {
				// this char should be \n
				if (ch == 10) {
					// handle header
					// remove \r\n by setting r to \0
					header[r] = '\0';

					if (strlen(header) == 1) {
						//printf("End of headers\n");
						throughHeaders = 1;
					} else {
						totalHeaders++;

						//printf("Got Header len %lu: %s\n", strlen(header), header);

						// check for Content-Length: header
						if (strncmp("Content-Length:", header, 15) == 0) {
							// set contentLength to this header
							rr = 15;
							int temp = 0;
							char tempc[10];
							while (1) {
								if (rr == strlen(header)) {
									break;
								}
								tempc[temp] = header[rr];
								temp++;
								rr++;
							}
							contentLength = atol(tempc);
							//printf("contentLength: %lli\n", contentLength);
							r = 0;

							if (contentLength == 0) {
								break;
							}
						} else if (strncmp("Transfer-Encoding: chunked", header, 26) == 0) {
							contentLength = -1;
							if (filesize > 0) {
								// chunked responses cannot resume with Range
								printf("%s is an existing file and this server is giving a chunked response which does not support range requests, please remove the file.\n", filename);
								break;
							}
						} else if (strncmp("Accept-Ranges: bytes", header, 20) == 0) {
							// this server can restart downloads with a Range: request header
							canRange = 1;
						} else if (strncmp("Location:", header, 9) == 0) {
							// this is a redirect
							int temp = 0;
							int temp2 = 10;
							while (1) {
								if (strlen(header) == temp2) {
									break;
								}
								requestUrl[temp] = header[temp2];
								temp++;
								temp2++;
							}
							requestUrl[temp-1] = '\0';
							printf("Got Redirect to: %s\n\n", requestUrl);
							shutdown(fd, SHUT_RDWR); 
							close(fd); 
							fclose(file);
							if (filesize == 0) {
								// only remove the file if the size is 0, it might be a resume of a file with the same name and different location
								remove(filename);
							}
							makeRequest();
						}

					}

					// reset header counter and nl
					r = 0;
					nl = 0;

					continue;
				} else {
					// wasn't \n, reset nl
					nl = 0;
				}
			}

			r++;
		} else {
			// this should be content
			if (contentLength == -1) {
				// this is a chunked response
				printf("Chunked responses are currently not supported\n");
				exit(EXIT_FAILURE);
				break;
				if (currentChunk == 0) {
					// up to \r\n will be the chunk length
				}

			} else {

				if (filesize > 0 && canRange == 0) {
					// this is a program restart with an existing file and the server doesn't support range requests
					// rather than just removing the file, let the user do that in case there is some data he wants
					printf("%s is an existing file and this server does not support range requests, please remove the file.\n", filename);
					fclose(file);
					exit(EXIT_FAILURE);
				}

				if (totalDownloaded >= contentLength) {
					// end of data for content-length response
					break;
				}
				// write it to file
				putc(ch, file);

				// update fiveRate
				if ((unsigned)time(NULL)-fiveRateTs >= 5) {
					sprintf(fiveRate, "%llu kBps(5s)", ((totalDownloaded-fiveRateB)/((unsigned)time(NULL)-fiveRateTs))/1000);
					fiveRateB = totalDownloaded;
					fiveRateTs = (unsigned)time(NULL);
				}

				// update oneRate and display every second
				if ((unsigned)time(NULL)-oneRateTs >= 1) {

					sprintf(oneRate, "%llu kBps(1s)", ((totalDownloaded-oneRateB)/((unsigned)time(NULL)-oneRateTs))/1000);
					oneRateB = totalDownloaded;
					oneRateTs = (unsigned)time(NULL);

					// strlen has a problem that tabs are not counted right
					// so we have to use spaces not tabs to print otherwise the backspace won't work
					sprintf(displayString, "%.5f%%    %llu bytes          %s    %s", (float)(totalDownloaded+filesize)/(float)(contentLength+filesize)*(float)100, totalDownloaded+filesize, fiveRate, oneRate);

					if (lastDisplayLen == 0) {
						printf("Got %u Response Headers\n", totalHeaders);
						if (canRange == 1) {
							printf("\n##### Downloading file %s with Range support for auto-resume of partial downloads #####\n", filename);
						} else {
							printf("\n##### Downloading file %s WITHOUT Range SUPPORT, DOWNLOADS CANNOT RESUME #####\n", filename);
						}
					}

					if (lastDisplayLen>0) {
						rr = 0;
						while (rr < lastDisplayLen) {
							// backspace that many characters, \b leaves trailing characters from before which were longer than the newly printed line
							// \b \b replaces them with a space
							printf("\b \b");
							rr++;
						}
					}

					// print at most 80 characters
					printf("%.*s", 80, displayString);
					// something's fucky if you don't use this
					fflush(stdout);
					lastDisplayLen = strlen(displayString);
				}

				//printf("%c", ch);
				r++;
				totalDownloaded++;
			}
		}

		bzero(buffer, BUFFER_SIZE);
		//fprintf(stderr, "%c", buffer[0]);

	}
	// show the cursor
	//fputs("\e[" "?25h", stdout);

	shutdown(fd, SHUT_RDWR); 
	close(fd); 
	fclose(file);

	if (contentLength == -2) {
		// never got contentLength header or chunked response
		// try again
		printf("Never got Content-Length Header\n");
		makeRequest();
	}

	if (totalDownloaded >= contentLength) {
		rr = 0;
		while (rr < lastDisplayLen) {
			// backspace that many characters, \b leaves trailing characters from before which were longer than the newly printed line
			// \b \b replaces them with a space
			printf("\b \b");
			rr++;
		}
		printf("Download Succeeded!\n");
		exit(EXIT_SUCCESS);
	}

	// there's no newline on the status
	printf("\n\n");

	if (canRange == 1) {
		printf("%s not fully downloaded but server accepts Range requests, automatically restarting\n", filename);
		makeRequest();
	} else if (contentLength == 0 || totalDownloaded == 0) {
		// there was nothing actually downloaded, the request can be remade
		makeRequest();
	} else if (filesize > 0) {
		// this would be the case of an automatic restart not being possible, this would not be called if it was a restart with an existing file
		printf("%s not fully downloaded, server does not accept Range requests (what a travesty) so http-dl cannot automatically restart\n", filename);
	}

}

int main(int argc, char *argv[]){

	if(argc < 2){
		fprintf(stderr, "Usage: %s http://hostname:port/path/to/file\n", argv[0]);
		exit(1); 
	}

	strcpy(requestUrl, argv[1]);

	makeRequest();

	return 0;
}
