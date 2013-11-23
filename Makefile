all:
	gcc -g readdir1.c -o readdir1
	gcc -g readdir2.c -o readdir2
	g++ -rdynamic -g -Wall syncopy.c -o syncopy
	g++ -rdynamic -g -Wall falloccopy.c -o falloccopy
	g++ -rdynamic -g -Wall laiocopy.c -lrt -o laiocopy

clean:
	rm -f readdir1
	rm -f readdir2
	rm -f syncopy
	rm -f falloccopy
	rm -f laiocopy
