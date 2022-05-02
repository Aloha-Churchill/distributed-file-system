/*
Distributed file client. Commands are get, list, put. Current implementation is designed for
4 servers.

put: client divides a file into sections and writes it to separate servers so that there is redundancy
list: client queries list of full files on servers
get: client reconstructs file from server storage

Base client connection from Beej's guide. 
*/

#include "helpers.h"

#define CONF_FILE_SIZE 4000
#define CONF_LINE_SIZE 512
#define NUM_SERVERS 4

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void send_put_header(int sockfd, char* filename, char* directory, int chunk_size){
    // format: put directory filename filesize\n
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

    int header_size = HEADER_SIZE;

    int res = sendall(sockfd, put_header, &header_size);
    if(res < 0){
        error("Could not sendall\n");
    }

}

/*
Takes the md5 hash of the filename and use this to get a server index in which to start the storage of the file
*/
int md5_hash(char* original_name){
    MD5_CTX c;

    unsigned char out[MD5_DIGEST_LENGTH];

    MD5_Init(&c);
    MD5_Update(&c, original_name, strlen(original_name));
    MD5_Final(out, &c);

    // code from https://stackoverflow.com/questions/11180028/converting-md5-result-into-an-integer-in-c
    unsigned long long v1 = *(unsigned long long*)(out);
    unsigned long long v2 = *(unsigned long long*)(out + 8);
    unsigned long long v3 = *(unsigned long long*)(out + 16);
    unsigned long long v4 = *(unsigned long long*)(out + 24);

    int hash = v1 ^ v2 ^ v3 ^v4;
    int bucket = abs(hash % 4);
    return bucket;
}

/*
Sends a put command to the client
*/
void send_put(int sockfd, char* filename, char* folder, FILE* file_fp, int file_chunk_size){
    send_put_header(sockfd, filename, folder, file_chunk_size);
    int num_s = read_file_send(sockfd, file_fp, file_chunk_size);
}

/*
Sends a get request to the client and writes the file to the local directory get_files/
*/
void send_get(int sockfd, char* dir_name, char* filename, FILE* fp){
    char get_request[HEADER_SIZE];
    bzero(get_request, HEADER_SIZE);
    strcpy(get_request, "get ");
    strcat(get_request, filename);
    strcat(get_request, " ");
    strcat(get_request, dir_name);
    strcat(get_request, "\n");

    // set request to server
    int header_size = HEADER_SIZE;
    sendall(sockfd, get_request, &header_size);

    char filesize_str[32];
    bzero(filesize_str, 32);

    // recieve filesize
    if(recv(sockfd, filesize_str, 32, 0) < 0){
        error("Error in recv\n");
    }
    int filesize = atoi(filesize_str);

    // recieve and write file
    int num_r = recv_write_file(sockfd, fp, filesize);
}

/*
Sends a list request to the server.
This method recieves the entire list of files from the server's directory and writes it to a local text file.
*/
void send_list(int sockfd, char* dir_name, FILE* list_fp){
    char list_request[HEADER_SIZE];
    bzero(list_request, HEADER_SIZE);

    strcpy(list_request, "list ");
    strcat(list_request, dir_name);
    strcat(list_request, "\n");

    printf("Header is: %s\n", list_request);

    int header_size = HEADER_SIZE;
    sendall(sockfd, list_request, &header_size);

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

/*
Method to connect to specific server based on conf file. Based on the option, sends a get, list, or put request to server
*/
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

    // nonblocking implementation from http://developerweb.net/viewtopic.php?id=3196. 
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

    // if able to connect to server, then send request based on option
    if(server_available == 0){
        if(option == 0){
            inet_ntop(servinfo->ai_family, get_in_addr((struct sockaddr *)servinfo->ai_addr), s, sizeof s);
            send_put(sockfd, filename, dir_names[serv_index], file_fp, file_chunk_size);
        }
        if(option == 1){
            send_get(sockfd, dir_names[serv_index], filename, file_fp);
        }
        if(option == 2){
            send_list(sockfd, dir_names[serv_index], file_fp);
        }
    }
    else{
        return -1;
    }
    freeaddrinfo(servinfo);
	close(sockfd);
    return 0;

}

/*
List files by reading from local temp file with all of the partial filenames and then sorting.
*/
void list_files(char* dir_names[], char* port_nums[], char* ip_addrs[]){
    FILE* list_fp = fopen("list_file.txt", "w+");
    if(list_fp == NULL){
        error("Error with fopen\n");
    }

    for(int i=0; i<NUM_SERVERS; i++){
        int ret = connect_to_server(2, i, dir_names, ip_addrs, port_nums, NULL, list_fp, 0);
        if(ret < 0){
            printf("Could not connect to server\n");
        }
    }
    fclose(list_fp);
    
    FILE* final_list_fp = fopen("final_list.txt", "w+");
    if(final_list_fp == NULL){
        error("Error with fopen\n");
    }
    
    // sort list according to filename and storage index number and remove duplicates
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

    // create a new local file of the final list of files that are fully or not fully constructable
    while(fgets(filename, HEADER_SIZE, sort_fp) != NULL){
        if(i == 0){
            strcpy(prev_filename, filename);
        }
        else{

            // find how many filenames were a match --> there should be 4 for a completely reconstructable file

            if(strncmp(prev_filename, filename, strlen(filename) - 2) == 0){
                num_sections += 1;    
            }
            else{
                char store_buf[HEADER_SIZE];
                bzero(store_buf, HEADER_SIZE);

                strncpy(store_buf, prev_filename, strlen(prev_filename)-2);

                if(num_sections >= NUM_SERVERS-1){
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

/*
Get files first utilizes the list command, and then sends request based on the directory and partial file name 
*/
void get_files(char* filename, char* dir_names[], char* port_nums[], char* ip_addrs[]){

    char full_filepath[HEADER_SIZE];
    bzero(full_filepath, HEADER_SIZE);
    strcpy(full_filepath, "get_files/");
    strcat(full_filepath, filename);
    FILE* fp = fopen(full_filepath, "w+"); //w+

    char num_command_buf[HEADER_SIZE];
    bzero(num_command_buf, HEADER_SIZE);
    strcpy(num_command_buf, "sort -k1,1 -fsu list_file.txt | grep ");
    strcat(num_command_buf, filename);
    strcat(num_command_buf, " | wc -l");

    // finding if file is reconstructable by getting the number of unique file chunks --> if this is not 4, then incomplete
    FILE* wc_fp = popen(num_command_buf, "r");
    if(wc_fp == NULL){
        error("Error in popen\n");
    }

    // getting filesize
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
        strcpy(command_buf, "sort -k1,1 -fsu list_file.txt | grep ");
        strcat(command_buf, filename);

        FILE* get_fp = popen(command_buf, "r");
        if(get_fp == NULL){
            error("Error in popen\n");
        }

        char get_header[HEADER_SIZE];
        bzero(get_header, HEADER_SIZE);

        // find the server number that corresponds to the current directory
        while(fgets(get_header, HEADER_SIZE, get_fp) != NULL){
        
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
            connect_to_server(1, serv_index, dir_names, ip_addrs, port_nums, filename, fp, 0);
            bzero(get_header, HEADER_SIZE);
        }

        pclose(get_fp);
    }
    fclose(fp);

}

/*
Put: process file, get length, perform MD5 hash to find which server to connect to first
*/
void put_files(char* filename, char* dir_names[], char* port_nums[], char* ip_addrs[]){
    int file_positions[4];
    int file_sizes[4];
    int ret;
    int hash = md5_hash(filename);

    FILE *file_fp = fopen(filename, "r");
    if(file_fp == NULL){
        error("Could not open file\n");
    }

    int filesize = get_file_size(file_fp);

    int chunk_size = filesize/NUM_SERVERS;
    int remainder = filesize%NUM_SERVERS;
    
    for(int i=0; i<4; i++){
        if(i == 3){
            file_sizes[i] = chunk_size + remainder;
        }
        else{
            file_sizes[i] = chunk_size;
        }
        file_positions[i] = i*chunk_size; 
    }

    const char indices[] = {'0', '1', '2', '3'};

    char send_filename[HEADER_SIZE];
    bzero(send_filename, HEADER_SIZE);
    strcpy(send_filename, filename);

    int all_servers_up = 0;
    for(int i=0; i<NUM_SERVERS; i++){
        int index = modulo(i-hash, NUM_SERVERS);

        send_filename[strlen(filename)] = indices[index];
        printf("index: %d\n", index);
        printf("send_filename: %s\n", send_filename);

        fseek(file_fp, file_positions[index], SEEK_SET);
        int file_chunk_size = file_sizes[index];

        ret = connect_to_server(0, i, dir_names, ip_addrs, port_nums, send_filename, file_fp, file_chunk_size);
        if(ret < 0){
            all_servers_up = -1;
            break;
        }
        
        index = modulo(i-hash+1, NUM_SERVERS);
        send_filename[strlen(filename)] = indices[index];

        fseek(file_fp, file_positions[index], SEEK_SET);
        file_chunk_size = file_sizes[index];

        ret = connect_to_server(0, i, dir_names, ip_addrs, port_nums, send_filename, file_fp, file_chunk_size);
        if(ret < 0){
            all_servers_up = -1;
            break;
        }
    }
    if(all_servers_up == -1){
        printf("Could not put file since all servers are not up\n");
    }
    else{
        printf("Put file successfully\n");
    }
    fclose(file_fp);
}

/*
Helper function to parse the configuration file
*/
void parse_configuration(char* dir_names[], char* port_nums[], char* ip_addrs[]){
    FILE *fp = fopen("dfc.conf", "r");
    if(fp == NULL){
        error("Could not open file");
    }

    char conf_buf[CONF_FILE_SIZE];
    bzero(conf_buf, CONF_FILE_SIZE);
    int conf_filesize = get_file_size(fp);

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

/*
Main function parses user input and calls correct command or throws error.
*/
int main(int argc, char *argv[])
{
 
	if (argc < 2) {
	    error("./dfc [command] [filename] ... [filename]");
	}

    char* dir_names[NUM_SERVERS];
    char* port_nums[NUM_SERVERS];
    char* ip_addrs[NUM_SERVERS];

    if(strncmp(argv[1], "list", 4) == 0){

        if(argc > 2){
            error("Incorrect number of arguments for list\n");
        }  
        parse_configuration(dir_names, port_nums, ip_addrs);
        list_files(dir_names, port_nums, ip_addrs);

        FILE* fp = fopen("final_list.txt", "r");
        if(fp == NULL){
            error("Could not get fp\n");
        }
        char buffer[HEADER_SIZE];
        bzero(buffer, HEADER_SIZE);

        while(fgets(buffer, HEADER_SIZE, fp)){
            buffer[strcspn(buffer, "\n")] = 0;
            printf("%s\n", buffer);
            bzero(buffer, HEADER_SIZE);
        }
        fclose(fp);
    }
    else if(strncmp(argv[1], "get", 3) == 0){

        for(int i=2; i<argc; i++){
            parse_configuration(dir_names, port_nums, ip_addrs);
            list_files(dir_names, port_nums, ip_addrs);
            parse_configuration(dir_names, port_nums, ip_addrs);
            get_files(argv[i], dir_names, port_nums, ip_addrs);
        }
    }
    else if(strncmp(argv[1], "put", 3) == 0){

        for(int i=2; i<argc; i++){
            parse_configuration(dir_names, port_nums, ip_addrs);
            put_files(argv[i], dir_names, port_nums, ip_addrs);
            
        }
    }
    else{
        error("Invalid commmand: choose either list, get, or put");
    }
	return 0;
}