#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

struct termios og_termios;

void disableRawMode(){
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios);
}

void enableRawMode(){
	tcgetattr(STDIN_FILENO, &og_termios);
	atexit(disableRawMode);	
	
	struct termios raw = og_termios;
	raw.c_lflag &= ~(ECHO);
	
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main(){
	enableRawMode();

	char c;
	while( read(STDIN_FILENO, &c, 1) == 1 );
	return 0;
}
