/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include "helpers.h"

//#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold
#define FILE_SIZE_PART 1
#define HEADER_SIZE 256

void parse_header(char* header_buf, char* parsed_elements[]){
	printf("Header buf: %s\n", header_buf);
	const char delimiters[] = " \n";
	char* element = strtok(header_buf, delimiters);
	int num_input_strings = 0;

	while(element != NULL){
		parsed_elements[num_input_strings] = element;
		element = strtok(NULL, delimiters);
		num_input_strings += 1;
	}	
}

void put_file(int sockfd, char* dir_name, char* filename, char* chunk_size){
	// open a new file with name filename in directory specified
	char full_filepath[HEADER_SIZE];
	bzero(full_filepath, HEADER_SIZE);
	strcpy(full_filepath, dir_name);
	strcat(full_filepath, "/");
	strcat(full_filepath, "filename");

	FILE* fp = fopen(full_filepath, "w+");
	if(fp == NULL){
		error("File could not be created\n");
	}

	int file_chunk_size = atoi(chunk_size);	
	char recvbuf[FILE_SIZE_PART];
	bzero(recvbuf, FILE_SIZE_PART);

	int num_recieves = file_chunk_size/FILE_SIZE_PART + ((file_chunk_size % FILE_SIZE_PART) != 0);
	for(int i=0; i< num_recieves; i++){
		// write each recieve to a file;
		recv(sockfd, recvbuf, FILE_SIZE_PART, 0);
		fwrite(recvbuf, 1, FILE_SIZE_PART, fp);
		bzero(recvbuf, FILE_SIZE_PART);
	}
	
	if (send(sockfd, "Hello, world!", 13, 0) == -1){
		error("Could not send\n");
	}

	fclose(fp);
}

void handle_client(int sockfd){

	// first read header that client sends
	char header_buf[HEADER_SIZE];
	bzero(header_buf, HEADER_SIZE);
	recv(sockfd, header_buf, HEADER_SIZE, 0);

	char* parsed_elements[4];
	parse_header(header_buf, parsed_elements);
	for(int i=0; i<4; i++){
		printf("parsed elements: %s\n", parsed_elements[i]);
	}

	if(strncmp(parsed_elements[0], "put", 3) == 0){
		put_file(sockfd, parsed_elements[1], parsed_elements[2], parsed_elements[3]);
	}

}

void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char* argv[])
{
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	if(argc < 3){
		error("Error, use form: ./dfs [folder] [portno] &");
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, argv[2], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	if ((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1) {
		perror("server: socket");
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		error("Error in setsockopt\n");
	}

	if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
		error("Error in bind");
	}


	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			handle_client(new_fd);
			close(new_fd);
			exit(0);
		}
		close(new_fd);  // parent doesn't need this
	}

	return 0;
}
