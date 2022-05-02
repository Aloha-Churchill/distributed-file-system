# Distributed File System
Implementation of client and server for a distributed file system with N=4 servers.
## How it works
### Client
* put: client divides a file into sections and writes it to separate servers so that there is redundancy
* list: client queries list of full files on servers
* get: client reconstructs file from server storage


### Server
* put: server stores file chunk into given directory
* list: server sends list of files in directory
* get: server sends selected filename
## Run
1. Define configuration file
server [FOLDER] [IP]:[PORT]

2. Run server
```make server```
```make run_server```

3. Run client
```make client```
```make run_client```

### Example localhost configuration
To run all servers on localhost, define each server with a different folder and port number.

## Clean
```make clean_server```
```make clean_client```

## Notes
Code has only been tested on 4 servers.

