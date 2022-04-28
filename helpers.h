/*
Wrapper function for error messages
*/
void error(char* message){
    perror(message);
    exit(1);
}