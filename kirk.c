/* --- includes --- */
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/* --- preprocessor --- */
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define KIRK_VERSION "0.0.1"

/* --- data --- */
struct editorConfig{
	int cx, cy; //cursor positions
	int screenrows, screencols;
	struct termios og_termios;
};

struct abuf{
	char *b;
	int len;
};

struct editorConfig E;

/* --- terminal --- */
void die(const char *s){
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);	

	perror(s);
	exit(EXIT_FAILURE);
}

void disableRawMode(){
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.og_termios) == -1){
		die("tcsetattr");
	}
}

void enableRawMode(){
	if(tcgetattr(STDIN_FILENO, &E.og_termios) == -1){
		die("tcgetattr");
	}
	atexit(disableRawMode);	
	
	struct termios raw = E.og_termios;
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

char editorReadKey(){
	int nread;
	char c;
	while( (nread = read(STDIN_FILENO, &c, 1)) != 1 ){
		if( nread == -1 && errno != EAGAIN )
			die("read");
	}
	return c;
}

int getCursorPosition(int *rows, int * cols){
	char buf[32];
	unsigned int i;

	if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;

	i = 0;
	while(i < sizeof(buf) -1){
		if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if(buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';

	if( buf[0] != '\x1b' || buf[1] != '[' )
		return -1;
	if( sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols){
	struct winsize ws;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
		if( write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12 )
			return -1;
		return getCursorPosition(rows, cols);	
	}else{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/* -- append buffer --- */
void abAppend(struct abuf *ab, const char *s, int len){
	char * new = realloc( ab->b, ab->len + len );
	
	if(new == NULL)
		return;

	memcpy( &new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf * ab){
	free(ab->b);
}

/* --- output --- */
void editorDrawRows(struct abuf *ab){
	int y;
	char welcome[80];
	int welcomelen;
	int padding;

	for(y = 0; y < E.screenrows-1; y++){
		if( y == E.screenrows / 3 ){
			welcomelen = snprintf(welcome, sizeof(welcome),
				"Kirk editor -- version %s", KIRK_VERSION);
			if(welcomelen > E.screencols)
				welcomelen = E.screencols;
			padding = (E.screencols - welcomelen) / 2;
			if(padding){
				abAppend(ab, "~", 1);
				padding --;
			}
			while(padding--)
				abAppend(ab, " ", 1);
			abAppend(ab, welcome, welcomelen);	
		}else{
			abAppend(ab, "~\x1b[K\r\n", 6);
		}
	}
	abAppend(ab, "~\x1b[K", 4);
}

void editorRefreshScreen(){
	char buf[32];
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);
	
	editorDrawRows(&ab);
	
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));
	
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/* --- input --- */
void editorMoveCursor(char key){
	switch(key){
		case 'a':
			E.cx--;
			break;
		case 'd':
			E.cx++;
			break;
		case 'w':
			E.cy--;
			break;
		case 's':
			E.cy++;
			break;
	}
}

void editorProcessKeypress(){
	char c;
	c = editorReadKey();

	switch(c){
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(EXIT_SUCCESS);
			break;
		
		case 'w':
		case 's':
		case 'a':
		case 'd':
			editorMoveCursor(c);
			break;
	}
}

/* --- init --- */
void initEditor(){
	E.cx = 0;
	E.cy = 0;
	if(getWindowSize(&E.screenrows, &E.screencols) == -1){
		die("getWindowSize");
	}
}

int main(){
	enableRawMode();
	initEditor();

	while( 1 ){
		editorRefreshScreen();
		editorProcessKeypress();	
	}

	return 0;
}
