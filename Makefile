COMPILER=gcc
FLAGS=-g -Wall


run_server:
	./dfs dfs1 10001 &
	./dfs dfs2 10002 &
	./dfs dfs3 10003 &
	./dfs dfs4 10004 &

run_client:
	./dfc put testfile.txt

# ./dfs ./dfs1 10001 & --> executable, folder, port, background process
# first run all of the dfs servers, then the dfc client
	

client: dfc.c
	$(COMPILER) $(FLAGS) -o dfc dfc.c -lcrypto -lssl

server: dfs.c
	$(COMPILER) $(FLAGS) -o dfs dfs.c

clean_client:
	rm dfc
clean_server:
	rm dfs