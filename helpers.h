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

#define FILE_SIZE_PART 1000
#define HEADER_SIZE 256
#define FILE_SIZE_STR 32

/*
Wrapper function for error messages
*/
void error(char* message){
    perror(message);
    exit(1);
}

/*
helper function from Beej's guide to network programming
*/
int sendall(int s, char *buf, int *len){
    int total = 0; // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n;

    while(total < *len) {
        n = send(s, buf+total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }
    *len = total; // return number actually sent here
    return n==-1?-1:0; // return -1 on failure, 0 on success
}

/*
helper function to get file size
*/
int get_file_size(FILE* file_fp){
    int filesize = -1;
    fseek(file_fp, 0L, SEEK_END);
    filesize = ftell(file_fp);
    fseek(file_fp, 0L, SEEK_SET);

    if(filesize == -1){
        error("Could not get length of file");
    }

    return filesize;    
}

/*
helper function to read from a local file and then send it to client/server over tcp
*/
int read_file_send(int sockfd, FILE* file_fp, int file_chunk_size){
    int total_num_sent = 0;

    int num_sends = file_chunk_size/FILE_SIZE_PART + ((file_chunk_size % FILE_SIZE_PART) != 0); 
    char file_contents[FILE_SIZE_PART];
    int n;
    int res;

    for(int i=0; i < num_sends; i++){
        bzero(file_contents, FILE_SIZE_PART);
        if(i == num_sends - 1){
            // send remainder of file
            n = fread(file_contents, 1, file_chunk_size%FILE_SIZE_PART, file_fp);
        }
        else{
            n = fread(file_contents, 1, FILE_SIZE_PART, file_fp);
        }
        
        if( n < 0){
            error("Could not fread\n");
        }
        res = sendall(sockfd, file_contents, &n);
        if(res < 0){
            error("error in sendall\n");
        }
        total_num_sent += n;
    }

    return total_num_sent;
}

/*
helper function to recieve from client/server and then write to local file
*/
int recv_write_file(int sockfd, FILE* fp, int file_chunk_size){
	int total_num_written = 0;
    
    //int num_recieves = file_chunk_size/FILE_SIZE_PART + ((file_chunk_size % FILE_SIZE_PART) != 0);
    char recvbuf[FILE_SIZE_PART];
    int n;
    int nw;

    while(total_num_written < file_chunk_size){
        bzero(recvbuf, FILE_SIZE_PART);
        n = recv(sockfd, recvbuf, FILE_SIZE_PART, 0);
        
        if(n < 0){
            error("Error in recv\n");
        }
        if((nw = fwrite(recvbuf, 1, n, fp)) < 0){
            error("Error in fwrite\n");
        }		
        total_num_written += n;
    } 

    return total_num_written;
}

/*
Helper function that takes modulus instead of remainder
from https://stackoverflow.com/questions/11720656/modulo-operation-with-negative-numbers
*/
int modulo(int x,int N){
    return (x % N + N) %N;
}
