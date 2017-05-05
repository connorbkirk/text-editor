/* --- includes --- */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>

/* --- preprocessor --- */
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define KIRK_VERSION "0.0.1"

/* --- data --- */
enum editorKey{
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

typedef struct erow{
	int size;
	char * chars;
} erow;

struct editorConfig{
	int cx, cy; //cursor positions
	int rowoff, coloff;
	int screenrows, screencols;
	int numrows;
	erow *row;
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

int editorReadKey(){
	int nread;
	char c;
	char seq[3];

	while( (nread = read(STDIN_FILENO, &c, 1)) != 1 ){
		if( nread == -1 && errno != EAGAIN )
			die("read");
	}

	if( c == '\x1b' ){
		if(read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if(read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';

		if(seq[0] == '['){
			if(seq[1] >= '0' && seq[1] <= '9'){
				if(read(STDIN_FILENO, &seq[2], 1) !=1)
					return '\x1b';
				if(seq[2] == '~'){
					switch(seq[1]){
						case '1':
							return HOME_KEY;
						case '2':
							return DEL_KEY;
						case '4':
							return END_KEY;
						case '5':
							return PAGE_UP;
						case '6':
							return PAGE_DOWN;
						case '7':
							return HOME_KEY;
						case '8':
							return END_KEY;
					}
				}
			}else{
				switch(seq[1]){
					case 'A':
						return ARROW_UP;
					case 'B':
						return ARROW_DOWN;
					case 'C':
						return ARROW_RIGHT;
					case 'D':
						return ARROW_LEFT;
					case 'H':
						return HOME_KEY;
					case 'F':
						return END_KEY;
				}
			}
		}
		else if(seq[0] == '0'){
			switch(seq[1]){
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
			}
		}
	
		return '\x1b';
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

/* -- row operations -- */
void editorAppendRow(char *s, size_t len){
	int at;

	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
	at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len+1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	E.numrows++;
}

/* -- file i/o -- */
void editorOpen(char * filename){
	FILE *fp;
	char *line;
	size_t linecap;
	ssize_t linelen;

	fp = fopen(filename, "r");
	if(!fp)
		die("fopen");

	line = NULL;
	linecap = 0;
	while((linelen = getline(&line, &linecap, fp)) != -1){
		while(linelen > 0 && 
		(line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
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

void editorScroll(){
	if(E.cy < E.rowoff){
		E.rowoff = E.cy;
	}
	if(E.cy >= E.rowoff + E.screenrows){
		E.rowoff = E.cy - E.screenrows + 1;
	}
	if(E.cx < E.coloff){
		E.coloff = E.cx;
	}
	if(E.cx >= E.coloff + E.screencols){
		E.coloff = E.cx - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf *ab){
	int y;
	int len;
	int padding;
	int filerow;
	char welcome[80];

	for(y = 0; y < E.screenrows; y++){
		filerow = y + E.rowoff;
		if(filerow >= E.numrows){
			if(E.numrows == 0 && y == E.screenrows / 3){
				len = snprintf(welcome, sizeof(welcome),
					"Kirk editor -- version %s", KIRK_VERSION);
				if(len > E.screencols)
					len = E.screencols;
				padding = (E.screencols - len)/2;
				if(padding){
					abAppend(ab, "~", 1);
					padding--;
				}

				while(padding --)
					abAppend(ab, " ", 1);
				abAppend(ab, welcome, len);
			}else{
				abAppend(ab, "~", 1);
			}
		}else{
			len = E.row[filerow].size - E.coloff;
			if(len < 0)
				len = 0;
			if(len > E.screencols)
				len = E.screencols;
			abAppend(ab, &E.row[filerow].chars[E.coloff], len);
		}

		abAppend(ab, "\x1b[K", 3);
		if(y < E.screenrows - 1)
			abAppend(ab, "\r\n", 2);
		
	}
}

void editorRefreshScreen(){
	char buf[32];
	struct abuf ab = ABUF_INIT;

	editorScroll();

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);
	
	editorDrawRows(&ab);
	
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));
	
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/* --- input --- */
void editorMoveCursor(int key){
	erow * row;
	int rowlen;

	if(E.cy >= E.numrows){
		row = NULL;
	}
	else{
		row = &E.row[E.cy];
	}
	
	switch(key){
		case ARROW_LEFT:
			if(E.cx!=0){
				E.cx--;
			}else if(E.cy > 0){
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if(row && E.cx < row->size){
				E.cx++;
			}else if(row && E.cx == row->size){
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if(E.cy != 0){
				E.cy--;
			}
			break;
		case ARROW_DOWN:
			if(E.cy < E.numrows){
				E.cy++;
			}
			break;
	}

	if(E.cy >= E.numrows){
		row = NULL;
		rowlen = 0;
	}else{
		row = &E.row[E.cy];
		rowlen = row->size;
	}
	if(E.cx > rowlen){
		E.cx = rowlen;
	}
}

void editorProcessKeypress(){
	int c, i;
	c = editorReadKey();

	switch(c){
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(EXIT_SUCCESS);
			break;
		
		case HOME_KEY:
			E.cx = 0;
			break;
		
		case END_KEY:
			E.cx = E.screencols - 1;
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			for(i = E.screenrows; i >= 0; i--)
				editorMoveCursor( c == PAGE_UP ? ARROW_UP: ARROW_DOWN);
			break;
		
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

/* --- init --- */
void initEditor(){
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;

	if(getWindowSize(&E.screenrows, &E.screencols) == -1){
		die("getWindowSize");
	}
}

int main(int argc, char *argv[]){
	enableRawMode();
	initEditor();
	if(argc >= 2){
		editorOpen(argv[1]);
	}

	while( 1 ){
		editorRefreshScreen();
		editorProcessKeypress();	
	}

	return 0;
}
