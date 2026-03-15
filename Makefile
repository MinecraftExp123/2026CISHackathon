all:
	gcc -O2 -o macro main.c

debug:
	gcc -g -Og -o macro main.c

clean:
	rm -rf macro main.dSYM
