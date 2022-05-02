/*

Distributed file server. Commands are get, list, put. Current implementation is designed for
4 servers.

put: server stores file chunk into given directory
list: server sends list of files in directory
get: server sends selected filename

Base server connection from Beej's guide. 

*/

#include <sys/wait.h>
#include <signal.h>

#include "helpers.h"

#define BACKLOG 10	 // how many pending connections queue will hold

/*
Parses command given from client.
*/
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

/*
Writes a file in the directory given
*/
void put_file(int sockfd, char* dir_name, char* filename, char* chunk_size){
	// open a new file with name filename in directory specified
	char full_filepath[HEADER_SIZE];
	bzero(full_filepath, HEADER_SIZE);
	strcpy(full_filepath, dir_name);
	strcat(full_filepath, "/");
	strcat(full_filepath, filename);

	FILE* fp = fopen(full_filepath, "w+");
	if(fp == NULL){
		error("File could not be created\n");
	}

	int file_chunk_size = atoi(chunk_size);	
	int num_r = recv_write_file(sockfd, fp, file_chunk_size);
    printf("Num bytes recv: %d\n", num_r);

	fclose(fp);
}

/*
Sends a file in the directory given
*/
void get_file(int sockfd, char* filename, char* dirname){
	// open file, read and then send to client
	char full_filepath[HEADER_SIZE];
	bzero(full_filepath, HEADER_SIZE);
	strcpy(full_filepath, dirname);
	strcat(full_filepath, "/");
	strcat(full_filepath, filename);

	FILE* fp = fopen(full_filepath, "r");  //r
	if(fp == NULL){
		error("Could not open file\n");
	}

	int filesize = get_file_size(fp);

	// first send client the filesize
	char filesize_str[FILE_SIZE_STR];
    bzero(filesize_str, FILE_SIZE_STR);
    sprintf(filesize_str, "%d", filesize);

	int file_size_string = FILE_SIZE_STR;
	sendall(sockfd, filesize_str, &file_size_string);

	int num_s = read_file_send(sockfd, fp, filesize);
    printf("Num bytes sent: %d\n", num_s);
}

/*
Lists filenames in the directory given
*/
void list_files(int sockfd, char* dirname){
	printf("In list files\n");
	//loop through all directories and remove all instances of that filename
	char send_fin[HEADER_SIZE];
	bzero(send_fin, HEADER_SIZE);
	strcpy(send_fin, "\r\n\r\n");

	char ls_command[HEADER_SIZE];
	bzero(ls_command, HEADER_SIZE);
	strcpy(ls_command, "ls -R ");
	strcat(ls_command, dirname);

	FILE *ls_fp = popen(ls_command, "r");
	if(ls_fp == NULL){
		error("Could not ls\n");
	}

	char curr_file[HEADER_SIZE];
	bzero(curr_file, HEADER_SIZE);
	int i = 0;
	while(fgets(curr_file, HEADER_SIZE, ls_fp) != NULL){
		if(i != 0){
			char file_dir[HEADER_SIZE];
			bzero(file_dir, HEADER_SIZE);
			strncpy(file_dir, curr_file, strlen(curr_file) - 1);
			strcat(file_dir, " ");
			strcat(file_dir, dirname);
			strcat(file_dir, "\n");
			// printf("curr_file: %s\n", curr_file);
			send(sockfd, file_dir, HEADER_SIZE, 0);
		}
		i = i+1;
	}
	send(sockfd, send_fin, HEADER_SIZE, 0);
	pclose(ls_fp);	
}

/*
Function called after fork to handle client
*/
void handle_client(int sockfd){
	// first read header that client sends
	char header_buf[HEADER_SIZE];
	bzero(header_buf, HEADER_SIZE);

	int n = recv(sockfd, header_buf, HEADER_SIZE, 0);
	if(n < 0){
		error("Error in recieving header\n");
	}
	if(n != HEADER_SIZE){
		printf("Did not recieve enough of header\n");
	}

	char* parsed_elements[4];
	parse_header(header_buf, parsed_elements);

	if(strncmp(parsed_elements[0], "put", 3) == 0){
		put_file(sockfd, parsed_elements[1], parsed_elements[2], parsed_elements[3]);
	}

	if(strncmp(parsed_elements[0], "list", 4) == 0){
		list_files(sockfd, parsed_elements[1]);
	}

	if(strncmp(parsed_elements[0], "get", 3) == 0){
		get_file(sockfd, parsed_elements[1], parsed_elements[2]);
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
