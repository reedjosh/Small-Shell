all: smallsh.c
	gcc smallsh.c -o smallsh

clean: 
	rm smallsh junk
