#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#define VERSION   "0"
#define TAB_STOP   8
#define QUIT_TIMES 3

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1F)

#define length(array) (sizeof(array) / sizeof((array)[0]))

#define HIGHLIGHT_NUMBERS (1 << 0)
#define HIGHLIGHT_STRINGS (1 << 1)

typedef struct {
  char*  file_type;
  char** file_match;
  char** keywords;
  char*  single_line_comment_start;
  char*  multi_line_comment_start;
  char*  multi_line_comment_end;
  int    flags;
} Syntax;

static char* c_extensions[] = { ".c", ".h", ".cpp", NULL };
static char* c_keywords[] = {
  "switch", "if",      "while",   "for",    "break", "continue",  "return",  "else",
  "struct", "union",   "typedef", "static", "enum",  "class",     "case",    "extern",
  "int|",   "long|",   "double|", "float|", "char|", "unsigned|", "signed|", "void|",
  NULL,
};

static char* haskell_extensions[] = { ".hs", NULL };
static char* haskell_keywords[] = {
  "!",        "'",         "\"",      "-",       "->",        "::",       ";",       "<-",
  ",",        "=",         "=>",      ">",       "?",         "#",        "*",       "@",
  "\\",       "_",         "as|",     "case|",   "of|",       "class|",   "data|",   "family|",
  "default|", "deriving|", "do|",     "forall|", "instance|", "foreign|", "hiding|", "if|",
  "then|",    "else|",     "import|", "infix|",  "infixl|",   "infixr|",  "let|",    "in|",
  "module|",  "newtype|",  "type|",   "where|",  NULL,
};

static Syntax syntaxes[] = {
  {
    "c",
    c_extensions,
    c_keywords,
    "//",
    "/*",
    "*/",
    HIGHLIGHT_NUMBERS | HIGHLIGHT_STRINGS
  },
  {
    "haskell",
    haskell_extensions,
    haskell_keywords,
    "--",
    "{-",
    "-}",
    HIGHLIGHT_NUMBERS | HIGHLIGHT_STRINGS
  },
};

static struct termios original;

static void clear_screen();

static void die(const char* s) {
  clear_screen();
  perror(s);
  exit(EXIT_FAILURE);
}

static void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == -1) {
    die("tcsetattr");
  }
}

static void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &original) == -1) {
    die("tcgetattr");
  }
  atexit(disable_raw_mode);
  
  struct termios raw = original;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO  | ICANON | IEXTEN | ISIG);
  
  raw.c_cc[VMIN]  = 0;
  raw.c_cc[VTIME] = 1;
  
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
      die("tcsetattr");
  }
}

#define BACKSPACE   0x7F
#define ARROW_UP    0xF0
#define ARROW_DOWN  0xF1
#define ARROW_RIGHT 0xF2
#define ARROW_LEFT  0xF3
#define PAGE_UP     0xF4
#define PAGE_DOWN   0xF5
#define HOME_KEY    0xF6
#define END_KEY     0xF7
#define DELETE_KEY  0xF8

static int read_key() {
  int key        = 0;
  int bytes_read = 0;
  while (bytes_read != 1) {
   bytes_read = read(STDIN_FILENO, &key, 1);
   if (bytes_read == -1 && errno != EAGAIN) {
     die("read");
   }
  }
  if (key == 0x1B) {
    char sequence[3] = {};
    if (read(STDIN_FILENO, &sequence[0], 1) == 1) {
      if (read(STDIN_FILENO, &sequence[1], 1) == 1) {
	if (sequence[0] == '[') {
	  if ('0' <= sequence[1] && sequence[1] <= '9') {
	    if (read(STDIN_FILENO, &sequence[2], 1) == 1) {
	      if (sequence[2] == '~') {
		if (sequence[1] == '5') {
		  key = PAGE_UP;
		}
		if (sequence[1] == '6') {
		  key = PAGE_DOWN;
		}
		if (sequence[1] == '1' || sequence[1] == '7') {
		  key = HOME_KEY;
		}
		if (sequence[1] == '4' || sequence[1] == '8') {
		  key = END_KEY;
		}
		if (sequence[1] == '3') {
		  key = DELETE_KEY;
		}
	      }
	    }
	  }
	  if (sequence[1] == 'A') {
	    key = ARROW_UP;
	  }
	  if (sequence[1] == 'B') {
	    key = ARROW_DOWN;
	  }
	  if (sequence[1] == 'C') {
	    key = ARROW_RIGHT;
	  }
	  if (sequence[1] == 'D') {
	    key = ARROW_LEFT;
	  }
	  if (sequence[1] == 'H') {
	    key = HOME_KEY;
	  }
	  if (sequence[1] == 'F') {
	    key = END_KEY;
	  }
	} else if (sequence[0] == 'O') {
	  if (sequence[1] == 'H') {
	    key = HOME_KEY;
	  }
	  if (sequence[1] == 'F') {
	    key = END_KEY;
	  }	  
	}
      }
    }
  }
  return key;
}

static int get_cursor_position(int* rows, int* columns) {
  int  result     = -1;
  char buffer[32] = {};

  if (write(STDOUT_FILENO, "\x1b[6n", 4) == 4) {
    for (int i = 0; i < sizeof(buffer) - 1; i++) {
      if (read(STDIN_FILENO, &buffer[i], 1) != 1 || buffer[i] == 'R') {
	break;
      }
    }

    if (buffer[0] == 0x1b && buffer[1] == '[') {
      if (sscanf(&buffer[2], "%d;%d", rows, columns) == 2) {
	result = 0;
      }
    }
  }
  
  return result;
}

static int get_window_size(int* rows, int* columns) {
  struct winsize size   = {};
  int            result = -1;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1 || size.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) == 12) {
      result = get_cursor_position(rows, columns);
    }
  } else {
    *rows    = size.ws_row;
    *columns = size.ws_col;
    result   = 0;
  }
  return result;
}

typedef struct {
  char* data;
  int   size;
  int   capacity;
} Buffer;

static void buffer_append(Buffer* buffer, char* message, int message_size) {
  while (buffer->size + message_size > buffer->capacity) {
    buffer->capacity = buffer->capacity == 0 ? 1 : (buffer->capacity * 2);
    if (buffer->data == NULL) {
      buffer->data = malloc(buffer->capacity);
    } else {
      buffer->data = realloc(buffer->data, buffer->capacity);
    }
  }
  if (message_size > 0) {
    memcpy(&buffer->data[buffer->size], message, message_size);
    buffer->size += message_size;
  }
}

#define HIGHLIGHT_NORMAL   0
#define HIGHLIGHT_COMMENT  1
#define HIGHLIGHT_COMMENTS 2
#define HIGHLIGHT_KEYWORD1 3
#define HIGHLIGHT_KEYWORD2 4
#define HIGHLIGHT_STRING   5
#define HIGHLIGHT_NUMBER   6
#define HIGHLIGHT_MATCH    7

typedef struct {
  int   index;
  char* data;
  int   size;
  char* rendered;
  int   rendered_size;
  char* highlights;
  int   open_comment;
} Row;

typedef struct {
  Row* data;
  int  size;
  int  capacity;
} Rows;

typedef struct {
  char*  file_name;

  Buffer buffer;
  int    rows;
  int    columns;

  int    row_offset;
  int    column_offset;
  
  int    cursor_x;
  int    cursor_y;
  int    rendered_x;
  
  int    row_count;
  Row*   row;

  char   message[80];
  time_t message_time;

  int dirty;
  int quit_times;

  int last_match;
  int direction;

  int   saved_highlight_line;
  char* saved_highlight;

  Syntax* syntax;
} Editor;

static void set_message(Editor* editor, const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(editor->message, sizeof(editor->message), format, arguments);
  va_end(arguments);
  
  editor->message_time = time(NULL);
}

static void refresh_screen(Editor* editor);

static char* ask(Editor* editor, char* prompt, void(*callback)(Editor*, char*, int)) {
  int   buffer_capacity = 128;
  int   buffer_size     = 0;
  char* buffer          = malloc(buffer_capacity);
  buffer[0]             = 0;
  
  while (1) {
    set_message(editor, prompt, buffer);
    refresh_screen(editor);

    int c = read_key();
    if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buffer_size > 0) {
	buffer_size--;
	buffer[buffer_size] = 0;
      }
    }

    else if (c == 0x1B) {
      set_message(editor, "");
      if (callback != NULL) {
	callback(editor, buffer, c);
      }
      free(buffer);
      return NULL;
    }
    
    else if (c == '\r') {
      set_message(editor, "");
      if (callback != NULL) {
	callback(editor, buffer, c);
      }
      return buffer;
    }

    else if (!iscntrl(c) && c < 128) {
      if (buffer_size == buffer_capacity - 1) {
	buffer_capacity *= 2;
	buffer           = realloc(buffer, buffer_capacity);
      }
      buffer[buffer_size] = c;
      buffer_size++;
      buffer[buffer_size] = 0;
    }

    if (callback != NULL) {
      callback(editor, buffer, c);
    }
  }
}

static int is_seperator(int c) {
  return isspace(c) || c == 0 || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

static void highlight_row(Editor* editor, Row* row) {
  if (row->rendered_size > 0) {
    if (row->highlights == NULL) {
      row->highlights = malloc(row->rendered_size);
    } else {
      row->highlights = realloc(row->highlights, row->rendered_size);
    }

    memset(row->highlights, HIGHLIGHT_NORMAL, row->rendered_size);
  }

  if (editor->syntax == NULL) {
    return;
  }

  Syntax* syntax                    = editor->syntax;
  char*   single_comment_start      = syntax->single_line_comment_start;
  int     single_comment_start_size = strlen(single_comment_start);
  char*   multi_comment_start       = syntax->multi_line_comment_start;
  int     multi_comment_start_size  = strlen(multi_comment_start);
  char*   multi_comment_end         = syntax->multi_line_comment_end;
  int     multi_comment_end_size    = strlen(multi_comment_end);
  char**  keywords                  = syntax->keywords;

  int previous_seperator = 1;
  int in_string          = 0;
  int in_comment         = row->index > 0 && editor->row[row->index - 1].open_comment;
  int index              = 0;
  while (index < row->rendered_size) {
    char c                  = row->rendered[index];
    int  previous_highlight = index == 0 ? HIGHLIGHT_NORMAL : row->highlights[index - 1];

    if (single_comment_start_size > 0 && !in_string && !in_comment) {
      if (!strncmp(&row->rendered[index], single_comment_start, single_comment_start_size)) {
	memset(&row->highlights[index], HIGHLIGHT_COMMENT, row->rendered_size - index);
	break;



      }
    }

    if (multi_comment_start_size > 0 && multi_comment_end_size > 0 && !in_string) {
      if (in_comment) {
	if (!strncmp(&row->rendered[index], multi_comment_end, multi_comment_end_size)) {
	  memset(&row->highlights[index], HIGHLIGHT_COMMENTS, multi_comment_end_size);
	  index += multi_comment_end_size;
	  in_comment = 0;
	  previous_seperator = 1;
	  continue;
	} else {
	  index++;
	  continue;
	}
      } else if (!strncmp(&row->rendered[index], multi_comment_start, multi_comment_start_size)) {
	memset(&row->highlights[index], HIGHLIGHT_COMMENTS, multi_comment_start_size);
	index += multi_comment_start_size;
	in_comment = 1;
	continue;
      }
    }

    if (syntax->flags & HIGHLIGHT_STRINGS) {
      if (in_string) {
	row->highlights[index] = HIGHLIGHT_STRING;
	if (c == '\'' && index + 1 < row->rendered_size) {
	  row->highlights[index + 1] = HIGHLIGHT_STRING;
	  index += 2;
	  continue;
	}
	if (c == in_string) {
	  in_string = 0;
	}
	previous_seperator = 0;
	index++;
	continue;
      } else {
	if (c == '"' || c == '\'') {
	  in_string              = c;
	  row->highlights[index] = HIGHLIGHT_STRING;
	  index++;
	  continue;
	}
      }
    }
    
    if (syntax->flags & HIGHLIGHT_NUMBERS) {
      int  previous_number    = previous_highlight == HIGHLIGHT_NUMBER;
      int  chained            = isdigit(c) && (previous_seperator || previous_number);
      if (chained || (c == '.' && previous_number)) {
	row->highlights[index] = HIGHLIGHT_NUMBER;
	previous_seperator     = 0;
	index++;
	continue;
      }
    }

    if (previous_seperator) {
      int found = 0;
      for (int i = 0; keywords[i] != NULL; i++) {
	char* keyword      = keywords[i];
	int   keyword_size = strlen(keywords[i]);
	int   is_keyword2  = keyword[keyword_size - 1] == '|';
	if (is_keyword2) {
	  keyword_size--;
	}

	if (strncmp(&row->rendered[index], keyword, keyword_size) == 0) {
	  if (is_seperator(row->rendered[index + keyword_size])) {
	    int highlight = is_keyword2 ? HIGHLIGHT_KEYWORD2 : HIGHLIGHT_KEYWORD1;
	    memset(&row->highlights[index], highlight, keyword_size);
	    index += keyword_size;
	    found  = 1;
	    break;
	  }
	}
      }

      if (found) {
	previous_seperator = 0;
	continue;
      }
    }

    previous_seperator = is_seperator(c);
    index++;
  }

  int changed       = row->open_comment != in_comment;
  row->open_comment = in_comment;
  if (changed && row->index + 1 < editor->row_count) {
    highlight_row(editor, &editor->row[row->index + 1]);
  }
}

static void select_syntax(Editor* editor) {
  editor->syntax = NULL;
  if (editor->file_name == NULL) {
    return;
  }

  char* extension = strchr(editor->file_name, '.');

  for (int i = 0; i < length(syntaxes); i++) {
    char** file_match = syntaxes[i].file_match;
    for (int j = 0; file_match[j] != NULL; j++) {
      int is_extension = file_match[j][0] == '.';
      int matched      = 0;
      if (is_extension) {
	if (extension != NULL && strcmp(extension, file_match[j]) == 0) {
	  editor->syntax = &syntaxes[i];
	  matched        = 1;
	}
      } else if (strcmp(editor->file_name, file_match[j])) {
	editor->syntax = &syntaxes[i];
	matched        = 1;
      }
      if (matched) {
	for (int i = 0; i < editor->row_count; i++) {
	  highlight_row(editor, &editor->row[i]);
	}
	break;
      }
    }
  }
}

static void render_row(Editor* editor, Row* row) {
  int tabs = 0;
  for (int i = 0; i < row->size; i++) {
    if (row->data[i] == '\t') {
      tabs++;
    }
  }
  
  free(row->rendered);
  row->rendered = malloc(row->size + (TAB_STOP - 1) * tabs + 1);

  int cursor = 0;
  for (int i = 0; i < row->size; i++) {
    if (row->data[i] == '\t') {
      do {
	row->rendered[cursor] = ' ';
	cursor++;
      } while (cursor % TAB_STOP != 0);
    } else {
      row->rendered[cursor] = row->data[i];
      cursor++;
    }
  }
  row->rendered[cursor] = 0;
  row->rendered_size    = cursor;

  highlight_row(editor, row);
}

static void insert_row(Editor* editor, char* text, int text_size, int at) {
  if (at < 0 || at > editor->row_count) {
    return;
  }
  
  if (editor->row == NULL) {
    editor->row = malloc(sizeof(Row));
  } else {
    editor->row = realloc(editor->row, sizeof(Row) * (editor->row_count + 1));
  }

  if (at != editor->row_count) {
    memmove(&editor->row[at + 1], &editor->row[at], sizeof(Row) * (editor->row_count - at));
  }

  for (int i = at + 1; i <= editor->row_count; i++) {
    editor->row[i].index++;
  }

  Row* row   = &editor->row[at];
  memset(row, 0, sizeof(Row));
  row->index = at;
  row->size  = text_size;
  row->data  = malloc(text_size + 1);
  memcpy(row->data, text, text_size);
  row->data[text_size] = 0;

  row->rendered      = NULL;
  row->rendered_size = 0;
  row->highlights    = NULL;
  row->open_comment  = 0;
  render_row(editor, row);

  editor->row_count++;
}

static void append_row(Editor* editor, char* text, int text_size) {
  insert_row(editor, text, text_size, editor->row_count);
}

static void delete_row(Editor* editor, int at) {
  if (0 <= at && at < editor->row_count) {
    Row* row = &editor->row[at];
    free(row->data);
    free(row->rendered);
    memmove(row, &editor->row[at + 1], sizeof(Row) * (editor->row_count - at - 1));
    for (int i = at + 1; i <= editor->row_count; i++) {
      editor->row[i].index--;
    }
    editor->row_count--;
  }
}

static void row_append_string(Editor* editor, Row* row, char* text, int text_size) {
  row->data = realloc(row->data, row->size + text_size + 1);
  memcpy(&row->data[row->size], text, text_size);
  row->size += text_size;
  row->data[row->size] = 0;
  render_row(editor, row);
}

static void insert_char(Editor* editor, Row* row, int at, char c) {
  if (at < 0 || at > row->size) {
    at = row->size;
  }
  row->data = realloc(row->data, row->size + 2);
  if (at != row->size) {
    memmove(&row->data[at + 1], &row->data[at], row->size - at + 1);
  }
  row->size++;
  row->data[at] = c;
  render_row(editor, row);
}

static void delete_char(Editor* editor, Row* row, int at) {
  if (0 <= at && at < row->size) {
    memmove(&row->data[at], &row->data[at + 1], row->size - at);
    row->size--;
    render_row(editor, row);
  }
}

static char* rows_to_string(Editor* editor, int* size_out) {
  int size = 0;
  for (int i = 0; i < editor->row_count; i++) {
    size += editor->row[i].size + 1;
  }
  char* result = malloc(size);
  int   cursor = 0;
  for (int i = 0; i < editor->row_count; i++) {
    Row* row = &editor->row[i];
    memcpy(&result[cursor], row->data, row->size);
    result[cursor + row->size] = '\n';
    cursor += row->size + 1;
  }
  *size_out = size;
  return result;
}

static void open_editor(Editor* editor) {
  select_syntax(editor);
  
  FILE* file = fopen(editor->file_name, "r");
  if (file == NULL) {
    die("fopen");
  }

  char*  line          = NULL;
  size_t line_capacity = 0;
  while (1) {
    ssize_t line_size = getline(&line, &line_capacity, file);
    if (line_size == -1) {
      break;
    }
    while (line_size > 0) {
      char c = line[line_size - 1];
      if (c != '\n' && c != '\r') {
	break;
      }
      line_size--;
    }
    append_row(editor, line, line_size);
  }

  free(line);
  fclose(file);
}

static void save_editor(Editor* editor) {
  if (editor->file_name == NULL) {
    editor->file_name = ask(editor, "Save as: %s (ESC to cancel)", NULL);
    if (editor->file_name == NULL) {
      set_message(editor, "Save aborted");
      return;
    }
    select_syntax(editor);
  }
  
  int   rows_size = 0;
  char* rows      = rows_to_string(editor, &rows_size);

  int fd = open(editor->file_name, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, rows_size) != -1) {
      int bytes_written = write(fd, rows, rows_size);
      if (bytes_written != -1) {
	set_message(editor, "%d/%d bytes written to disk", bytes_written, rows_size);
	editor->dirty = 0;
      }
    }
  }
  if (errno != 0) {
    set_message(editor, "Can't save! I/O error: %s", strerror(errno));
  }
  if (fd != -1) {
    close(fd);
  }
  free(rows);
}

static int to_unrendered_index(Row* row, int target_render_index) {
  int index        = 0;
  int render_index = 0;
  for (index = 0; index < row->size && render_index < target_render_index; index++) {
    if (row->data[index] == '\t') {
      render_index += (TAB_STOP - 1) - (render_index % TAB_STOP);
    }
    render_index++;
  }
  return index;
}

static void find_editor_callback(Editor* editor, char* query, int key) {
  if (editor->saved_highlight != NULL) {
    Row* row = &editor->row[editor->saved_highlight_line];
    memcpy(row->highlights, editor->saved_highlight, row->rendered_size);
    free(editor->saved_highlight);
    editor->saved_highlight = NULL;
  }
  
  if (key == '\r' || key == 0x1B) {
    return;
  }

  if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    editor->direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    editor->direction = -1;
  } else {
    editor->last_match = -1;
    editor->direction  = 1;
  }

  if (editor->last_match == -1) {
    editor->direction = 1;
  }
  int current = editor->last_match;
  
  for (int i = 0; i < editor->row_count; i++) {
    current += editor->direction;
    if (current == -1) {
      current = editor->row_count - 1;
    } else if (current == editor->row_count) {
      current = 0;
    }
    
    Row*  row   = &editor->row[current];
    char* match = strstr(row->rendered, query);
    if (match != NULL) {
      editor->last_match = current;
      editor->cursor_y   = current;
      editor->cursor_x   = to_unrendered_index(row, match - row->rendered);
      editor->row_offset = editor->row_count;

      editor->saved_highlight_line = current;
      editor->saved_highlight      = malloc(row->rendered_size);
      memcpy(editor->saved_highlight, row->highlights, row->rendered_size);
      memset(&row->highlights[match - row->rendered], HIGHLIGHT_MATCH, strlen(query));
      break;
    }
  }
}

static void find_editor(Editor* editor) {
  int cursor_x      = editor->cursor_x;
  int cursor_y      = editor->cursor_y;
  int column_offset = editor->column_offset;
  int row_offset    = editor->row_offset;

  editor->last_match = -1;
  editor->direction  = 1;

  char* query = ask(editor, "Search: %s (ESC/Arrows/Enter)", find_editor_callback);
  if (query == NULL) {
    editor->cursor_x	  = cursor_x;
    editor->cursor_y	  = cursor_y;
    editor->column_offset = column_offset;
    editor->row_offset	  = row_offset;
  } else {
    free(query);
  }
}

static void cursor_to_top_left() {
  write(STDOUT_FILENO, "\x1b[H",  3);
}

static void clear_screen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  cursor_to_top_left();
}

static void handle_key(Editor* editor, int c) {
  int row_size = 0;
  if (editor->cursor_y < editor->row_count) {
    row_size = editor->row[editor->cursor_y].size;
  }
  
  if (c == CTRL_KEY('q')) {
    if (!editor->dirty || editor->quit_times == QUIT_TIMES) {
      clear_screen();
      exit(EXIT_SUCCESS);
    }
    char* format =
      "WARNING!!! File has unsaved changes. "
      "Press Ctrl-Q %d more times to quit.";
    set_message(editor, format, QUIT_TIMES - editor->quit_times);
    editor->quit_times++;
  } else {
    editor->quit_times = 0;
  }
  if (c == CTRL_KEY('f')) {
    find_editor(editor);
  }
  if (c == CTRL_KEY('s')) {
    save_editor(editor);
  }
  if (c == ARROW_UP && editor->cursor_y > 0) {
    editor->cursor_y--;
  }
  if (c == ARROW_DOWN && editor->cursor_y <= editor->rows) {
    editor->cursor_y++;
  }
  if (c == ARROW_RIGHT) {
    if (editor->cursor_x < row_size) {
      editor->cursor_x++;      
    } else {
      editor->cursor_y++;
      editor->cursor_x = 0;
    }
  }
  if (c == ARROW_LEFT) {
    if (editor->cursor_x > 0) {
      editor->cursor_x--;
    } else if (editor->cursor_y > 0) {
      editor->cursor_y--;
      editor->cursor_x = INT_MAX;
    }
  }
  if (c == PAGE_UP) {
    editor->cursor_y = editor->row_offset;
  }
  if (c == PAGE_DOWN) {
    editor->cursor_y = editor->row_offset + editor->rows - 1;
    if (editor->cursor_y > editor->rows) {
      editor->cursor_y = editor->rows;
    }
  }
  if (c == HOME_KEY) {
    editor->cursor_x = 0;
  }
  if (c == END_KEY) {
    if (editor->cursor_y < editor->row_count) {
      editor->cursor_x = editor->row[editor->cursor_y].size;
    }
  }
  if (c == BACKSPACE || c == CTRL_KEY('h') || c == DELETE_KEY) {
    if (c == DELETE_KEY) {
      handle_key(editor, ARROW_RIGHT);
    }
    int in_bounds = editor->cursor_y < editor->row_count;
    int is_origin = editor->cursor_x == 0 && editor->cursor_y == 0;
    if (in_bounds && !is_origin) {
      if (editor->cursor_x > 0) {
	delete_char(editor, &editor->row[editor->cursor_y], editor->cursor_x - 1);
	editor->cursor_x--;
      } else {
	Row* old_row = &editor->row[editor->cursor_y];
	Row* new_row = &editor->row[editor->cursor_y - 1];
	editor->cursor_x = new_row->size;
	row_append_string(editor, new_row, old_row->data, old_row->size);
	delete_row(editor, editor->cursor_y);
	editor->cursor_y--;
      }
    }
    editor->dirty = 1;
  }
  if (c == '\r') {
    if (editor->cursor_x == 0) {
      insert_row(editor, "", 0, editor->cursor_y);
    } else {
      Row*  row  = &editor->row[editor->cursor_y];
      char* line = &row->data[editor->cursor_x];
      insert_row(editor, line, row->size - editor->cursor_x, editor->cursor_y + 1);
      
      row                  = &editor->row[editor->cursor_y];
      row->size            = editor->cursor_x;
      row->data[row->size] = 0;
      render_row(editor, row);
    }
    editor->cursor_y++;
    editor->cursor_x = 0;
  }
  if (isprint(c)) {
    if (editor->cursor_y == editor->row_count) {
      append_row(editor, "", 0);
    }
    insert_char(editor, &editor->row[editor->cursor_y], editor->cursor_x, c);
    editor->dirty = 1;
    editor->cursor_x++;
  }

  row_size = 0;
  if (editor->cursor_y < editor->row_count) {
    row_size = editor->row[editor->cursor_y].size;
  }
  if (editor->cursor_x > row_size) {
    editor->cursor_x = row_size;
  }
}

static void refresh_screen(Editor* editor) {
  Buffer* buffer = &editor->buffer;
  
  int rows     = editor->rows;
  int columns  = editor->columns;
  int cursor_x = editor->cursor_x;
  int cursor_y = editor->cursor_y;

  editor->rendered_x = 0;
  if (cursor_y < editor->row_count) {
    for (int i = 0; i < cursor_x; i++) {
      if (editor->row[cursor_y].data[i] == '\t') {
	editor->rendered_x += (TAB_STOP - 1) - (editor->rendered_x % TAB_STOP);
      }
      editor->rendered_x++;
    }
  }

  if (cursor_y < editor->row_offset) {
    editor->row_offset = cursor_y;
  }
  if (cursor_y > editor->row_offset + rows) {
    editor->row_offset = cursor_y - editor->rows + 1;
  }
  if (editor->rendered_x < editor->column_offset) {
    editor->column_offset = editor->rendered_x;
  }
  if (editor->rendered_x > editor->column_offset + columns) {
    editor->column_offset = editor->rendered_x - editor->columns + 1;
  }
  
  buffer->size = 0;
  buffer_append(buffer, "\x1b[?25l", 6); // Hide cursor while refreshing.
  buffer_append(buffer, "\x1b[H",    3); // Move cursor to top left.

  for (int y = 0; y < rows; y++) {
    int file_row = y + editor->row_offset;
    
    if (file_row < editor->row_count) {
      Row* row  = &editor->row[file_row];
      int  size = row->rendered_size - editor->column_offset;
      if (size < 0) {
	size = 0;
      }
      if (size > editor->columns) {
	size = editor->columns;
      }
      int current_color = -1;
      for (int i = editor->column_offset; i < size; i++) {
	int highlight = row->highlights[i];
	if (iscntrl(row->rendered[i])) {
	  char symbol = (row->rendered[i] <= 26) ? ('@' + row->rendered[i]) : '?';
	  buffer_append(buffer, "\x1b[7m", 4); // Invert colors.
	  buffer_append(buffer, &symbol, 1);
	  buffer_append(buffer, "\x1b[m", 3); // Reset formatting.
	  if (current_color != -1) {
	    char command[16]  = {};
	    int  command_size = snprintf(command, sizeof(command), "\x1b[%dm", current_color);
	    buffer_append(buffer, command, command_size);
	  }
	} else if (highlight == HIGHLIGHT_NORMAL) {
	  buffer_append(buffer, "\x1b[39m", 5); // Default color.
	  buffer_append(buffer, &row->rendered[i], 1);
	  current_color = -1;
	} else {
	  int color = 39;
	  if (highlight == HIGHLIGHT_COMMENT || highlight == HIGHLIGHT_COMMENTS) {
	    color = 36;
	  }
	  if (highlight == HIGHLIGHT_KEYWORD1) {
	    color = 33;
	  }
	  if (highlight == HIGHLIGHT_KEYWORD2) {
	    color = 32;
	  }
	  if (highlight == HIGHLIGHT_STRING) {
	    color = 35;
	  }
	  if (highlight == HIGHLIGHT_NUMBER) {
	    color = 31;
	  }
	  if (highlight == HIGHLIGHT_MATCH) {
	    color = 34;
	  }	  
	  if (color != current_color) {
	    current_color     = color;
	    char command[16]  = {};
	    int  command_size = snprintf(command, sizeof(command), "\x1b[%dm", color);
	    buffer_append(buffer, command, command_size);
	  }
	  buffer_append(buffer, &row->rendered[i], 1);
	}
      }
      buffer_append(buffer, "\x1b[39m", 5); // Default color.
    } else {
      if (y == rows / 3) {
	char* welcome      = "Editor1 -- Version " VERSION;
	int   welcome_size = strlen(welcome);
      
	if (welcome_size > columns) {
	  welcome_size = columns;
	}

	int padding = (columns - welcome_size) / 2;
	if (padding > 0) {
	  buffer_append(buffer, "~", 1);
	  padding--;
	}
	while (padding > 0) {
	  buffer_append(buffer, " ", 1);
	  padding--;
	}
      
	buffer_append(buffer, welcome, welcome_size);
      } else {
	buffer_append(buffer, "~", 1);
      }
    }
    
    buffer_append(buffer, "\x1b[K", 3); // Clear line.
    buffer_append(buffer, "\r\n",   2);
  }

  buffer_append(buffer, "\x1b[7m", 4); // Invert colors.
  
  char  status[80]  = {};
  char* file_name   = editor->file_name == NULL ? "[No Name]" : editor->file_name;
  char* modified    = editor->dirty ? "(modified)" : "";
  int   status_size = snprintf(
    status, sizeof(status), "%.20s - %d lines %s", file_name, editor->row_count, modified
  );
  buffer_append(buffer, status, status_size);

  char right_status[80]  = {};
  int  right_status_size = snprintf(
    right_status,
    sizeof(right_status),
    "%s | %d/%d",
    editor->syntax == NULL ? "no ft" : editor->syntax->file_type,
    editor->cursor_y + 1,
    editor->row_count
  );

  for (int i = status_size; i < editor->columns; i++) {
    if (editor->columns - i == right_status_size) {
      buffer_append(buffer, right_status, right_status_size);
      break;
    }
    buffer_append(buffer, " ", 1);
  }
  buffer_append(buffer, "\x1b[m", 3); // Reset color.
  buffer_append(buffer, "\r\n", 2);

  buffer_append(buffer, "\x1b[K", 3); // Clear line.
  int message_size = strlen(editor->message);
  if (message_size > editor->columns) {
    message_size = editor->columns;
  }
  if (message_size > 0 && time(NULL) - editor->message_time < 5) {
    buffer_append(buffer, editor->message, message_size);
  }

  int screen_y = cursor_y           - editor->row_offset    + 1;
  int screen_x = editor->rendered_x - editor->column_offset + 1;
  
  char move_cursor[32] = {};
  snprintf(move_cursor, sizeof(move_cursor), "\x1b[%d;%dH", screen_y, screen_x);
  buffer_append(buffer, move_cursor, strlen(move_cursor));
  
  buffer_append(buffer, "\x1b[?25h", 6); // Show cursor after refreshing.
  write(STDOUT_FILENO, buffer->data, buffer->size);
}

int main(int argc, char** argv) {
  enable_raw_mode();

  Editor editor = {};

  if (argc > 1) {
    editor.file_name = argv[1];
    open_editor(&editor);
  }
  
  if (get_window_size(&editor.rows, &editor.columns) == -1) {
    die("get_window_size");
  }
  editor.rows -= 2;

  set_message(&editor,"HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = search");
  
  while (1) {
    refresh_screen(&editor);
    int c = read_key();
    handle_key(&editor, c);
  }
}
