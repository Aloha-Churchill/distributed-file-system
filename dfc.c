/*
** client.c -- a stream socket client demo
1. Parse dfc.conf file to get list of servers, ip, and port no

// connection timeout after 1 second
// put file should be completed

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <openssl/md5.h>


#include "helpers.h"

#define PORT "3490" // the port client will be connecting to 
// instead, we will read dfc.conf file and parse the ip addresses and port numbers
// make file size part 1 so no remainders?
#define FILE_SIZE_PART 1
#define HEADER_SIZE 256

#define MAXDATASIZE 100 // max number of bytes we can get at once 
#define CONF_LINE_SIZE 512
#define NUM_SERVERS 4


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void send_put_header(int chunk_size, char* directory, char* filename, int sockfd){
    // format: put directory filename filesize\r\n\r\n
    char put_header[HEADER_SIZE];
    bzero(put_header, HEADER_SIZE);

    char filesize_str[32];
    bzero(filesize_str, 32);
    sprintf(filesize_str, "%d", chunk_size);

    strcpy(put_header, "put ");
    strcat(put_header, directory);
    strcat(put_header, " ");
    strcat(put_header, filename);
    strcat(put_header, " ");
    strcat(put_header, filesize_str);
    strcat(put_header, "\n");

    printf("Header is: %s\n", put_header);

    send(sockfd, put_header, HEADER_SIZE, 0);

}

void send_file(){

}

/*
Takes the md5 hash of the URI and converts it to a string representation
*/
int md5_hash(char* original_name){
    MD5_CTX c;

    unsigned char out[MD5_DIGEST_LENGTH];

    MD5_Init(&c);
    MD5_Update(&c, original_name, strlen(original_name));
    MD5_Final(out, &c);

    //int bucket = (int)strtol(out, NULL, 0) % 4;
    // code from https://stackoverflow.com/questions/11180028/converting-md5-result-into-an-integer-in-c
    unsigned long long v1 = *(unsigned long long*)(out);
    unsigned long long v2 = *(unsigned long long*)(out + 8);
    unsigned long long v3 = *(unsigned long long*)(out + 16);
    unsigned long long v4 = *(unsigned long long*)(out + 24);

    int hash = v1 ^ v2 ^ v3 ^v4;
    int bucket = abs(hash % 4);
    return bucket;

}

void connect_to_server(char* filename, char* folder, char* ip_addr, char* port, FILE* file_fp, int file_chunk_size){

    int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo;
	int rv;
	char s[INET6_ADDRSTRLEN];
    int res;
    int valopt;
    socklen_t lon;
    fd_set fdset;

    memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;


	if ((rv = getaddrinfo(ip_addr, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return;
	}

    res = getaddrinfo(ip_addr, port, &hints, &servinfo);
    if(res != 0){
        error("Could not get address information\n");
    }

    // create nonblocking socket that times out after 1s
    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(sockfd == -1){
        error("Could not create a socket\n");
    }
    int sockfd_nonblocking = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    // try to connect to server, times out after 1 second --> code from https://stackoverflow.com/questions/2597608/c-socket-connection-timeout
    // http://developerweb.net/viewtopic.php?id=3196.
    res = connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    int server_available = 0;

    if(res < 0){
        if(errno == EINPROGRESS){
            do {
                FD_ZERO(&fdset);
                FD_SET(sockfd, &fdset);
                
                struct timeval timeout;      
                timeout.tv_sec = 1;
                timeout.tv_usec = 0;

                res = select(sockfd + 1, NULL, &fdset, NULL, &timeout);

                if(res < 0 && errno != EINTR){
                    server_available = -1;
                    break;
                }
                else if(res > 0){
                    lon = sizeof(int); 
                    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) { 
                        server_available = -1;
                    } 
                    // Check the value returned... 
                    if (valopt) {
                        server_available = -1;
                    } 
                    break; 
                }
                else{
                    printf("Timeout\n");
                    server_available = -1;
                    break;
                }
            } while(1);
        }
        else {
            error("Different connection error\n");
        }
    }
    fcntl(sockfd, F_SETFL, sockfd_nonblocking);

    freeaddrinfo(servinfo); // all done with this structure

    if(server_available == 0){

        // if put is the option
        inet_ntop(servinfo->ai_family, get_in_addr((struct sockaddr *)servinfo->ai_addr), s, sizeof s);
        printf("client: connecting to %s\n", s);

        send_put_header(file_chunk_size, folder, filename, sockfd);

        // first we send a header to the server
        // then we send data one character at a time until chunk size is over
        int num_sends = file_chunk_size/FILE_SIZE_PART + ((file_chunk_size % FILE_SIZE_PART) != 0); 
        char file_contents[FILE_SIZE_PART];

        for(int i=0; i < num_sends; i++){
            bzero(file_contents, FILE_SIZE_PART);
            int n = fread(file_contents, 1, FILE_SIZE_PART, file_fp);

            if( n < 0){
                error("Could not fread\n");
            }

            if(send(sockfd, file_contents, n, 0) < 0){
                error("Send to client failed\n");
            }

        }

        if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
            perror("recv");
            exit(1);
        }

        buf[numbytes] = '\0';
        printf("client: received '%s'\n",buf);
    }
    else{
        printf("Server is unavailable\n");
    }
	close(sockfd);

}

// returns list of directories, ip addresses, and port numbers

void put_files(char* filename){
    // process file, get length, perform MD5 hash to find which server to connect to first
    int filesize;
    int file_positions[4];
    int file_sizes[4];
    int hash = md5_hash(filename);

    printf("Hash of file is: %d\n", hash);

    FILE *file_fp = fopen(filename, "r");
    if(file_fp == NULL){
        error("Could not open file\n");
    }
    fseek(file_fp, 0L, SEEK_END);
    filesize = ftell(file_fp);
    fseek(file_fp, 0L, SEEK_SET);

    int chunk_size = filesize/4;
    int remainder = filesize%4;

    printf("Chunk size of file: %d\n", chunk_size);
    printf("Remainder size of file: %d\n", remainder);
    
    for(int i=0; i<4; i++){
        if(i == 3){
            file_sizes[i] = chunk_size + remainder;
        }
        else{
            file_sizes[i] = chunk_size;
        }
        file_positions[i] = i*chunk_size; 
    }

    // array of fp positions 
    // curr_server_pos(i) + MD5Hash (starting server pos) % 4

    //fclose(fp);
    // connect to each server
    FILE *fp = fopen("dfc.conf", "r");
    if(fp == NULL){
        error("Could not open file");
    }

    char line[CONF_LINE_SIZE];
    bzero(line, CONF_LINE_SIZE);
    const char delimiters[] = ": ";

    char* dir_name = NULL;
    char* ip_addr = NULL;
    char* port_no = NULL;

    // still pretty sure need list so we can do hashing thing
    int i = 0;
    while(fgets(line, CONF_LINE_SIZE, fp)){  
        char* element = strtok(line, delimiters);
        int num_input_strings = 0;
        while(element != NULL){
            if(num_input_strings == 1){
                dir_name = element;
            }
            if(num_input_strings == 2){
                ip_addr = element;
            }
            if(num_input_strings == 3){
                port_no = element;
            }
            element = strtok(NULL, delimiters);
            num_input_strings += 1;
        }

        if(dir_name == NULL || ip_addr == NULL || port_no == NULL){
            error("Invalid configuration file\n");
        }

        port_no[strcspn(port_no, "\n")] = 0;

        int index = (i+hash)%4;
        fseek(file_fp, file_positions[index], SEEK_SET);
        int file_chunk_size = file_sizes[index];

        connect_to_server(filename, dir_name, ip_addr, port_no, file_fp, file_chunk_size);
        i += 1;
        // then we move the change the file pointer and chunksize
    }

    fclose(fp);
    fclose(file_fp);

}

int main(int argc, char *argv[])
{
	if (argc < 2) {
	    error("./dfc [command] [filename] ... [filename]");
	}

    if(strncmp(argv[1], "list", 4) == 0){

    }
    else if(strncmp(argv[1], "get", 3) == 0){
    }
    else if(strncmp(argv[1], "put", 3) == 0){
        printf("Here\n");
        // put files should take in a list of filenames
        // for each filename, put that file
        for(int i=2; i<argc; i++){
            put_files(argv[i]);
        }
        //put_files();
    }
    else{
        error("Invalid commmand: choose either list, get, or put");
    }
    //

	return 0;
}