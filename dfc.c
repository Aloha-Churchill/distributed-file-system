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
#define CONF_FILE_SIZE 4000

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

void send_file(int file_chunk_size, char* folder, char* filename, int sockfd, FILE* file_fp){
    //int numbytes;
    //char buf[MAXDATASIZE];

    // if put is the option
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

    /*
    if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
        perror("recv");
        exit(1);
    }

    buf[numbytes] = '\0';
    printf("client: received '%s'\n",buf);
    */
}



void send_get(int sockfd, char* dir_name, char* filename, FILE* fp){
    char get_request[HEADER_SIZE];
    bzero(get_request, HEADER_SIZE);
    strcpy(get_request, "get ");
    strcat(get_request, filename);
    strcat(get_request, " ");
    strcat(get_request, dir_name);
    strcat(get_request, "\n");

    printf("---------request: %s\n", get_request);

    send(sockfd, get_request, HEADER_SIZE, 0);

    char filesize_str[32];
    bzero(filesize_str, 32);

    recv(sockfd, filesize_str, 32, 0);

    int filesize = atoi(filesize_str);

    int num_recieves = filesize/FILE_SIZE_PART + ((filesize % FILE_SIZE_PART) != 0); 
    char file_contents[FILE_SIZE_PART];

    for(int i=0; i < num_recieves; i++){
        bzero(file_contents, FILE_SIZE_PART);
        int n = recv(sockfd, file_contents, FILE_SIZE_PART, 0);

        if( n < 0){
            error("Could not recv\n");
        }

        fwrite(file_contents, 1, FILE_SIZE_PART, fp);
    }
    
}

void send_list(int sockfd, char* dir_name, FILE* list_fp){
    char list_request[HEADER_SIZE];
    bzero(list_request, HEADER_SIZE);

    strcpy(list_request, "list ");
    strcat(list_request, dir_name);
    strcat(list_request, "\n");

    printf("Header is: %s\n", list_request);

    send(sockfd, list_request, HEADER_SIZE, 0);

    char filenames[HEADER_SIZE];
    bzero(filenames, HEADER_SIZE);

    while(1){
        if(recv(sockfd, filenames, HEADER_SIZE, 0) < 0){
            error("Error in recv\n");
        }
        if(strstr(filenames, "\r\n\r\n") != NULL){
            printf("stop from server\n");
            break;
        }

        printf("client got: %s\n", filenames);
        fwrite(filenames, strlen(filenames), 1, list_fp);
        bzero(filenames, HEADER_SIZE);
    } 
}

int connect_to_server(int option, int serv_index, char* dir_names[], char* ip_addrs[], char* port_nums[], char* filename, FILE* file_fp, int file_chunk_size){
    // options: 0 = put, 1 = get, 2 = list
    int sockfd;  
	struct addrinfo hints, *servinfo;
	char s[INET6_ADDRSTRLEN];
    int res;
    int valopt;
    socklen_t lon;
    fd_set fdset;

    memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

    printf("IP_ADDR: %s\n", ip_addrs[serv_index]);
    printf("PORT No: %s\n", port_nums[serv_index]);

    res = getaddrinfo(ip_addrs[serv_index], port_nums[serv_index], &hints, &servinfo);
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
        if(option == 0){
            inet_ntop(servinfo->ai_family, get_in_addr((struct sockaddr *)servinfo->ai_addr), s, sizeof s);
            printf("client: connecting to %s\n", s);
            send_file(file_chunk_size, dir_names[serv_index], filename, sockfd, file_fp);
        }
        if(option == 1){
            // get
            send_get(sockfd, dir_names[serv_index], filename, file_fp);
        }
        if(option == 2){
            send_list(sockfd, dir_names[serv_index], file_fp);
        }
    }
    else{
        return -1;
    }
	close(sockfd);
    return 0;

}


void list_files(char* dir_names[], char* port_nums[], char* ip_addrs[]){
    FILE* list_fp = fopen("list_file.txt", "w+");
    if(list_fp == NULL){
        error("Error with popen\n");
    }

    for(int i=0; i<NUM_SERVERS; i++){
        int ret = connect_to_server(2, i, dir_names, ip_addrs, port_nums, NULL, list_fp, 0);
        if(ret < 0){
            printf("Could not connect to server\n");
        }
    }

    
    FILE* final_list_fp = fopen("final_list.txt", "w+");
    if(final_list_fp == NULL){
        error("Error with popen\n");
    }
    
    fclose(list_fp);

    FILE* sort_fp = popen("awk '{print $1}' list_file.txt | sort -u", "r");
    if(sort_fp == NULL){
        error("Error in popen\n");
    }

    char filename[HEADER_SIZE];
    bzero(filename, HEADER_SIZE);

    int i=0;
    char prev_filename[HEADER_SIZE];
    bzero(prev_filename, HEADER_SIZE);

    int num_sections = 0;

    while(fgets(filename, HEADER_SIZE, sort_fp) != NULL){
        if(i == 0){
            strcpy(prev_filename, filename);
        }
        else{
            printf("Previous filename: %s\n", prev_filename);
            printf("Current filename: %s\n", filename);
            if(strncmp(prev_filename, filename, strlen(filename) - 2) == 0){
                num_sections += 1;    
            }
            else{
                printf("NAMES DID NOT MATCH\n");
                printf("NUMEBR OF SECTIONS: %d\n", num_sections);
                char store_buf[HEADER_SIZE];
                bzero(store_buf, HEADER_SIZE);

                strncpy(store_buf, prev_filename, strlen(prev_filename)-2);

                if(num_sections >= 3){
                    strcat(store_buf, "\n");
                    fwrite(store_buf, 1, strlen(store_buf), final_list_fp); 
                }
                else{
                    strcat(store_buf, " -- INCOMPLETE\n");
                    fwrite(store_buf, 1, strlen(store_buf) , final_list_fp);
                }
                num_sections = 0;
            }
        }
        bzero(prev_filename, HEADER_SIZE);
        strcpy(prev_filename, filename);
        i += 1;
        printf("Got: %s\n", filename);
    }
    char store_buf[HEADER_SIZE];
    bzero(store_buf, HEADER_SIZE);
    strncpy(store_buf, prev_filename, strlen(prev_filename)-2);

    if(num_sections >= 3){
        strcat(store_buf, "\n");
        fwrite(store_buf, 1, strlen(store_buf) , final_list_fp);
    }
    else{
        strcat(store_buf, " -- INCOMPLETE\n");
        fwrite(store_buf, 1, strlen(store_buf) , final_list_fp);   
    }

    pclose(sort_fp);
    fclose(final_list_fp);
}


void get_files(char* filename, char* dir_names[], char* port_nums[], char* ip_addrs[]){

    char full_filepath[HEADER_SIZE];
    bzero(full_filepath, HEADER_SIZE);
    strcpy(full_filepath, "get_files/");
    strcat(full_filepath, filename);
    FILE* fp = fopen(full_filepath, "w+");

    char num_command_buf[HEADER_SIZE];
    bzero(num_command_buf, HEADER_SIZE);
    strcpy(num_command_buf, "sort -k1,1 -fsu test_file.txt | grep ");
    strcat(num_command_buf, filename);
    strcat(num_command_buf, " | wc -l");

    FILE* wc_fp = popen(num_command_buf, "r");
    if(wc_fp == NULL){
        error("Error in popen\n");
    }
    char number[5];
    bzero(number, 5);
    int num_pieces = 0;
    while(fgets(number, 5, wc_fp) != NULL){
        num_pieces = atoi(number);
    }
    pclose(wc_fp);

    if(num_pieces < 4){
        printf("Not enough pieces to reconstruct file\n");
    }
    else{
        char command_buf[HEADER_SIZE];
        bzero(command_buf, HEADER_SIZE);
        strcpy(command_buf, "sort -k1,1 -fsu test_file.txt | grep ");
        strcat(command_buf, filename);

        FILE* get_fp = popen(command_buf, "r");
        if(get_fp == NULL){
            error("Error in popen\n");
        }

        char get_header[HEADER_SIZE];
        bzero(get_header, HEADER_SIZE);
        // find the server number that corresponds to the current directory

        while(fgets(get_header, HEADER_SIZE, get_fp) != NULL){
            printf("Sending: %s\n", get_header);
            
            //
            const char delimiters[] = " \n";
            char* element = strtok(get_header, delimiters);
            int i = 0;
            char *dirname;
            char *filename;
            int serv_index = -1;

            while(element != NULL){
                if(i == 0){
                    filename = element;
                }
                if(i == 1){
                    dirname = element;
                }
                element = strtok(NULL, delimiters);
                i += 1;
            }

            for(i=0; i<NUM_SERVERS; i++){
                printf("dirname: %s\n", dirname);
                printf("dir_names[i]: %s\n", dir_names[i]);
                if(strcmp(dirname, dir_names[i]) == 0){
                    serv_index = i;
                }
            }
            //
            printf("SERVER_INDEX IS: %d\n", serv_index);
            printf("FILENAME IS: %s\n", filename);
            connect_to_server(1, serv_index, dir_names, ip_addrs, port_nums, filename, fp, 0);
            bzero(get_header, HEADER_SIZE);
        }

        pclose(get_fp);
    }
    fclose(fp);

}


void put_files(char* filename, char* dir_names[], char* port_nums[], char* ip_addrs[]){
    // process file, get length, perform MD5 hash to find which server to connect to first
    int num_servers_up = 0; 
    int filesize;
    int file_positions[4];
    int file_sizes[4];
    int hash = md5_hash(filename);
    int ret;

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

    const char* indices[] = {"0", "1", "2", "3"};


    for(int i=0; i<NUM_SERVERS; i++){
        char filename_one[HEADER_SIZE];
        bzero(filename_one, HEADER_SIZE);
        strcpy(filename_one, filename);
        strcat(filename_one, indices[i]);

        int index = (i+hash)%4;
        fseek(file_fp, file_positions[index], SEEK_SET);
        int file_chunk_size = file_sizes[index];

        ret = connect_to_server(0, i, dir_names, ip_addrs, port_nums, filename_one, file_fp, file_chunk_size);
        if(ret < 0){
            if(num_servers_up > 0){
                // send_remove(sockfd, filename, dir_names);
            }
            break;
        }

        char filename_two[HEADER_SIZE];
        bzero(filename_two, HEADER_SIZE);
        strcpy(filename_two, filename);
        strcat(filename_two, indices[(i+1)%4]);
        
        index = (i+hash+1)%4;
        fseek(file_fp, file_positions[index], SEEK_SET);
        file_chunk_size = file_sizes[index];

        ret = connect_to_server(0, i, dir_names, ip_addrs, port_nums, filename_two, file_fp, file_chunk_size);
        if(ret < 0){
            if(num_servers_up > 0){
                // send_remove(sockfd, filename, dir_names);
            }
            break;
        }

        num_servers_up += 1;
    }

    fclose(file_fp);

}


void parse_configuration(char* dir_names[], char* port_nums[], char* ip_addrs[]){
    FILE *fp = fopen("dfc.conf", "r");
    if(fp == NULL){
        error("Could not open file");
    }

    char conf_buf[CONF_FILE_SIZE];
    bzero(conf_buf, CONF_FILE_SIZE);
    int conf_filesize;

    fseek(fp, 0L, SEEK_END);
    conf_filesize = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    if(fread(conf_buf, conf_filesize, 1, fp) < 0){
        error("Could not read configuration file\n");
    }

    const char delimiters[] = ": \n";
    char* element = strtok(conf_buf, delimiters);
    int num_input_strings = 0;
    int i = 0;

    while(element != NULL){

        if(num_input_strings%4 == 1){
            dir_names[i] = element;
        }
        if(num_input_strings%4 == 2){
            ip_addrs[i] = element;
        }
        if(num_input_strings%4 == 3){
            port_nums[i] = element;
        }
        element = strtok(NULL, delimiters);
        num_input_strings += 1;
        i = num_input_strings/4;
    }

    fclose(fp);    
}

int main(int argc, char *argv[])
{
    // read dfc.conf here and then pass it into methods
    // dfc.conf should create 3 separate lists. Dir list, ip list, port list

	if (argc < 2) {
	    error("./dfc [command] [filename] ... [filename]");
	}

    char* dir_names[NUM_SERVERS];
    char* port_nums[NUM_SERVERS];
    char* ip_addrs[NUM_SERVERS];

    parse_configuration(dir_names, port_nums, ip_addrs);

    if(strncmp(argv[1], "list", 4) == 0){
        printf("LIST COMMAND\n");
        if(argc > 2){
            error("Incorrect number of arguments for list\n");
        }  
        list_files(dir_names, port_nums, ip_addrs);
    }
    else if(strncmp(argv[1], "get", 3) == 0){
        printf("GET COMMAND\n");
        for(int i=2; i<argc; i++){
            list_files(dir_names, port_nums, ip_addrs);
            parse_configuration(dir_names, port_nums, ip_addrs);
            get_files(argv[i], dir_names, port_nums, ip_addrs);
        }
    }
    else if(strncmp(argv[1], "put", 3) == 0){
        printf("PUT COMMAND\n");
        // put files should take in a list of filenames
        // for each filename, put that file
        for(int i=2; i<argc; i++){
            put_files(argv[i], dir_names, port_nums, ip_addrs);
        }
    }
    else{
        error("Invalid commmand: choose either list, get, or put");
    }
    //

	return 0;
}