main : main.c fs.c
	cc -o main main.c fs.c

clean :
	-rm -rf main
