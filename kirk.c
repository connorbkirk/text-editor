#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

struct termios og_termios;

void die(const char *s){
	perror(s);
	exit(EXIT_FAILURE);
}

void disableRawMode(){
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios) == -1){
		die("tcsetattr");
	}
}

void enableRawMode(){
	if(tcgetattr(STDIN_FILENO, &og_termios) == -1){
		die("tcgetattr");
	}
	atexit(disableRawMode);	
	
	struct termios raw = og_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1){
		die("tcsetattr");
	}
}

int main(){
	enableRawMode();

	char c;
	while( 1 ){
		c = '\0';
		if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN){
			die("read");
		}
	
		if( iscntrl(c) ){
			printf("%d\r\n", c);
		}else{
			printf("%d ('%c')\r\n", c, c);
		}
		if( c == 'q' ) break;
	}

	return 0;
}
