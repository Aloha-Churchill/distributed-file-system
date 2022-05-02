COMPILER=gcc
FLAGS=-g -Wall

run_server:
	./dfs dfs1 10001 &
	./dfs dfs2 10002 &
	./dfs dfs3 10003 &
	./dfs dfs4 10004 &

run_client:
	./dfc list

client: dfc.c
	$(COMPILER) $(FLAGS) -o dfc dfc.c -lcrypto -lssl

server: dfs.c
	$(COMPILER) $(FLAGS) -o dfs dfs.c

clean_client:
	rm dfc

clean_server:
	killall dfs
	rm dfs
