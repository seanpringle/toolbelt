#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PRIME_1000 997
#define PRIME_10000 9973
#define PRIME_100000 99991
#define PRIME_1000000 999983

#define ensure(x) for ( ; !(x) ; exit(EXIT_FAILURE) )
#define errorf(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while(0)

#define min(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a: _b; })
#define max(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a: _b; })

typedef uint8_t byte_t;

void*
allocate (size_t bytes)
{
  void *ptr = malloc(bytes);
  ensure(ptr) errorf("malloc failed %lu bytes", bytes);
  return ptr;
}

void*
reallocate (void *ptr, size_t bytes)
{
  ptr = realloc(ptr, bytes);
  ensure(ptr) errorf("malloc failed %lu bytes", bytes);
  return ptr;
}

int
regmatch (regex_t *re, char *subject)
{
  return regexec(re, subject, 0, NULL, 0) == 0;
}

uint32_t
djb_hash (char *str)
{
  uint32_t hash = 5381;
  for (int i = 0; str[i]; hash = hash * 33 + str[i++]);
  return hash;
}

char*
str_fgets (FILE *file)
{
  size_t bytes = 100;
  char *line = allocate(bytes+1);
  line[0] = 0;

  while (!feof(file) && !ferror(file) && fgets(line + bytes - 100, 101, file) && !strchr(line + bytes - 100, '\n'))
  {
    bytes += 100;
    line = reallocate(line, bytes+1);
  }
  if (ferror(file) || (!line[0] && feof(file)))
  {
    free(line);
    line = NULL;
  }
  return line;
}

char*
strf (char *pattern, ...)
{
  char *result = NULL;
  va_list args;
  char buffer[8];

  va_start(args, pattern);
  int len = vsnprintf(buffer, sizeof(buffer), pattern, args);
  va_end(args);

  if (len > -1 && (result = allocate(len+1)) && result)
  {
    va_start(args, pattern);
    vsnprintf(result, len+1, pattern, args);
    va_end(args);
  }
  return result;
}

typedef struct { off_t index; char *subject; int l1; } str_each_t;

#define str_each(l,_val_) for ( \
  str_each_t loop = { 0, (l), 0 }; \
    loop.subject && !loop.l1 && loop.subject[loop.index] && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = loop.subject[loop.index]; loop.l1; loop.l1 = !loop.l1)

typedef int (*str_cb_ischar)(int);

int str_eq  (char *a, char *b) { return !strcmp(a, b); }
int str_ne  (char *a, char *b) { return  strcmp(a, b); }
int str_lt  (char *a, char *b) { return  strcmp(a, b) <  0; }
int str_lte (char *a, char *b) { return  strcmp(a, b) <= 0; }
int str_gt  (char *a, char *b) { return  strcmp(a, b) >  0; }
int str_gte (char *a, char *b) { return  strcmp(a, b) >= 0; }

int
str_skip (char *s, str_cb_ischar cb)
{
  int n = 0;
  str_each(s, char c)
  {
    if (!cb(c)) break;
    n++;
  }
  return n;
}

int
str_scan (char *s, str_cb_ischar cb)
{
  int n = 0;
  str_each(s, char c)
  {
    if (cb(c)) break;
    n++;
  }
  return n;
}

char*
str_trim (char *str, str_cb_ischar cb)
{
  char *left = str + str_skip(str, cb);
  size_t len = strlen(left);
  memmove(str, left, len+1);
  for (
    char *p = left + len - 1;
    p >= str && *p && cb(*p);
    *p = 0, p--
  );
  return str;
}

int istab (int c) { return c == '\t'; }
int iscomma (int c) { return c == ','; }
int isperiod (int c) { return c == '.'; }
int isforwardslash (int c) { return c == '/'; }
int isbackslash (int c) { return c == '\\'; }
int isdquote (int c) { return c == '"'; }
int issquote (int c) { return c == '\''; }
int isname (int c) { return isalnum(c) || c == '_'; }
int iscolon (int c) { return c == ':'; }
int issemicolon (int c) { return c == ';'; }

char*
str_copy (char *s, size_t length)
{
  char *a = allocate(length+1);
  memmove(a, s, length);
  a[length] = 0;
  return a;
}

#define STR_ENCODE_HEX 1
#define STR_ENCODE_SQL 2
#define STR_ENCODE_DQUOTE 3

char*
str_encode (char *s, int format)
{
  char *result = NULL;

  static char* hex256[256] = {
    "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0a", "0b", "0c", "0d", "0e", "0f",
    "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "1a", "1b", "1c", "1d", "1e", "1f",
    "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "2a", "2b", "2c", "2d", "2e", "2f",
    "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "3a", "3b", "3c", "3d", "3e", "3f",
    "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "4a", "4b", "4c", "4d", "4e", "4f",
    "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "5a", "5b", "5c", "5d", "5e", "5f",
    "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "6a", "6b", "6c", "6d", "6e", "6f",
    "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "7a", "7b", "7c", "7d", "7e", "7f",
    "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "8a", "8b", "8c", "8d", "8e", "8f",
    "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "9a", "9b", "9c", "9d", "9e", "9f",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "aa", "ab", "ac", "ad", "ae", "af",
    "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8", "b9", "ba", "bb", "bc", "bd", "be", "bf",
    "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8", "c9", "ca", "cb", "cc", "cd", "ce", "cf",
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "da", "db", "dc", "dd", "de", "df",
    "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8", "e9", "ea", "eb", "ec", "ed", "ee", "ef",
    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "fa", "fb", "fc", "fd", "fe", "ff",
  };

  if (format == STR_ENCODE_HEX)
  {
    int length = strlen(s);
    int bytes = length * 2 + 1;
    result = allocate(bytes);
    for (int i = 0; s[i]; i++)
      strcpy(&result[i*2], hex256[(unsigned char)s[i]]);
    result[bytes-1] = 0;
  }
  else
  if (format == STR_ENCODE_SQL)
  {
    int length = strlen(s) + 100;
    int bytes = length * 2 + 1;
    result = allocate(bytes);
    strcpy(result, "convert_from(decode('");
    int offset = strlen(result);
    for (int i = 0; s[i]; i++, offset += 2)
      strcpy(&result[offset], hex256[(unsigned char)s[i]]);
    strcpy(&result[offset], "', 'hex'), 'UTF8')");
  }
  else
  if (format == STR_ENCODE_DQUOTE)
  {
    int length = 0;
    result = allocate(8);

    result[length++] = '"';
    result[length] = 0;

    str_each(s, char c)
    {
      result = reallocate(result, length + 8);

           if (c == 0x07) { strcpy(result+length, "\\a"); length += 2; }
      else if (c == 0x08) { strcpy(result+length, "\\b"); length += 2; }
      else if (c == 0x0c) { strcpy(result+length, "\\f"); length += 2; }
      else if (c == 0x0a) { strcpy(result+length, "\\n"); length += 2; }
      else if (c == 0x0d) { strcpy(result+length, "\\r"); length += 2; }
      else if (c == 0x09) { strcpy(result+length, "\\t"); length += 2; }
      else if (c == 0x0B) { strcpy(result+length, "\\v"); length += 2; }
      else if (c == '\\') { strcpy(result+length, "\\\\"); length += 2; }
      else if (c ==  '"') { strcpy(result+length, "\\\""); length += 2; }
      else                { result[length++] = c; result[length] = 0; }
    }

    result = reallocate(result, length + 8);
    result[length++] = '"';
    result[length] = 0;
  }
  else
  {
    ensure(0)
      errorf("str_encode() unknown format: %d", format);
  }
  return result;
}

char*
str_decode (char *s, char **e, int format)
{
  char *result = NULL;
  if (format == STR_ENCODE_HEX)
  {
    char buf[3];
    int bytes = strlen(s) / 2 + 1;
    result = allocate(bytes);
    for (int i = 0; s[i]; i += 2)
    {
      strncpy(buf, &s[i], 2); buf[2] = 0;
      result[i/2] = strtol(buf, NULL, 16);
    }
    result[bytes-1] = 0;

    if (e)
      *e = s+bytes;
  }
  else
  if (format == STR_ENCODE_DQUOTE)
  {
    if (e)
      *e = s;

    if (*s++ != '"')
      goto done;

    size_t length = 0, limit = 32;
    result = allocate(32);

    str_each(s, char c)
    {
      if (e)
        *e = s + loop.index;

      if (c == '"')
      {
        if (e)
          *e = s + loop.index + 1;
        break;
      }

      if (c == '\\')
      {
        c = s[++loop.index];

             if (c == 'a')  c = '\a';
        else if (c == 'b')  c = '\b';
        else if (c == 'f')  c = '\f';
        else if (c == 'n')  c = '\n';
        else if (c == 'r')  c = '\r';
        else if (c == 't')  c = '\t';
        else if (c == 'v')  c = '\v';
      }

      if (length >= limit-1)
      {
        limit += 32;
        result = reallocate(result, limit+1);
      }

      result[length++] = c;
      result[length] = 0;
    }
  }
done:
  return result;
}

typedef struct _text_t {
  char *buffer;
  size_t bytes;
  off_t cursor;
} text_t;

static char *text_nil = "";

void
text_clear (text_t *text)
{
  if (text->buffer)
    free(text->buffer);

  text->buffer = NULL;
  text->bytes  = 0;
  text->cursor = 0;
}

void
text_free (text_t *text)
{
  text_clear(text);
  free(text);
}

char*
text_at (text_t *text, off_t pos)
{
  text->cursor = min(text->bytes - 1, max(0, pos));
  return &text->buffer[text->cursor];
}

char*
text_go (text_t *text, int offset)
{
  text->cursor = min(text->bytes - 1, max(0, (int64_t)text->cursor + offset));
  return &text->buffer[text->cursor];
}

off_t
text_pos (text_t *text)
{
  return text->cursor;
}

void
text_set (text_t *text, char *str)
{
  text_clear(text);

  text->bytes = strlen(str) + 1;
  text->buffer = allocate(text->bytes);
  text->cursor = 0;
  strcpy(text->buffer, str);
}

void
text_ins (text_t *text, char *str)
{
  if (!text->buffer)
  {
    text_set(text, str);
    text->cursor = text->bytes - 1;
    return;
  }

  size_t new_bytes = strlen(str);
  text->buffer = reallocate(text->buffer, text->bytes + new_bytes);
  memmove(&text->buffer[text->cursor + new_bytes], &text->buffer[text->cursor], text->bytes - text->cursor);
  memmove(&text->buffer[text->cursor], str, new_bytes);
  text->cursor += new_bytes;
  text->bytes += new_bytes;
}

void
text_del (text_t *text, size_t bytes)
{
  if (text->buffer)
  {
    bytes = min(text->bytes - text->cursor - 1, bytes);
    memmove(&text->buffer[text->cursor],
      &text->buffer[text->cursor + bytes],
      text->bytes - text->cursor - bytes
    );
    text->bytes -= bytes;
    text_at(text, text->cursor);
  }
}

size_t
text_format(text_t *text, char *pattern, ...)
{
  size_t bytes = 0;
  char *result = NULL;

  va_list args;
  char buffer[8];

  va_start(args, pattern);
  int len = vsnprintf(buffer, sizeof(buffer), pattern, args);
  va_end(args);

  if (len > -1 && (result = allocate(len+1)) && result)
  {
    va_start(args, pattern);
    bytes = vsnprintf(result, len+1, pattern, args) + 1;
    va_end(args);

    text_clear(text);
    text->bytes = bytes + 1;
    text->buffer = result;
    return bytes;
  }

  text_clear(text);
  return 0;
}

char*
text_get (text_t *text)
{
  return text->buffer ? text->buffer: text_nil;
}

int
text_cmp (text_t *text, char *str)
{
  return text->buffer ? strcmp(text->buffer, str): -1;
}

size_t
text_count (text_t *text)
{
  return text->buffer ? text->bytes-1: 0;
}

size_t
text_len (text_t *text)
{
  if (text->buffer)
  {
    size_t i = 0, j = 0;
    char *s = text->buffer;
    while (s[i])
    {
      if ((s[i] & 0xC0) != 0x80)
        j++;
      i++;
    }
    return j;
  }
  return 0;
}

off_t
text_scan (text_t *text, str_cb_ischar cb)
{
  if (text->buffer)
  {
    size_t n = str_scan(text->buffer + text->cursor, cb);
    text->cursor += n;
    return n;
  }
  return 0;
}

size_t
text_skip (text_t *text, str_cb_ischar cb)
{
  if (text->buffer)
  {
    size_t n = str_skip(text->buffer + text->cursor, cb);
    text->cursor += n;
    return n;
  }
  return 0;
}

size_t
text_find (text_t *text, char *str)
{
  if (text->buffer)
  {
    char *s = strstr(text->buffer + text->cursor, str);
    if (s)
    {
      text->cursor = s - text->buffer;
      return 1;
    }
  }
  return 0;
}

size_t
text_trim (text_t *text, str_cb_ischar cb)
{
  if (text->buffer)
  {
    str_trim(text->buffer, cb);
    text->bytes = strlen(text->buffer) + 1;
    text->cursor = min(text->bytes-1, text->cursor);
  }
  return max(text->bytes-1, 0);
}

#define TEXT_HEX STR_ENCODE_HEX
#define TEXT_SQL STR_ENCODE_SQL
#define TEXT_DQUOTE STR_ENCODE_DQUOTE

int
text_encode (text_t *text, int type)
{
  if (text->buffer)
  {
    char *result = str_encode(text->buffer, type);
    text_clear(text);
    text_set(text, result);
    return 1;
  }
  return 0;
}

int
text_decode (text_t *text, int type)
{
  if (text->buffer)
  {
    char *err = NULL;
    char *result = str_decode(text->buffer, &err, type);
    if (err == text->buffer + text->bytes - 1)
    {
      text_clear(text);
      text_set(text, result);
      return 1;
    }
  }
  return 0;
}

int
text_match (text_t *text, regex_t *re)
{
  return text->buffer ? regmatch(re, text->buffer + text->cursor): 0;
}

text_t*
text_init (text_t *text)
{
  text->buffer = NULL;
  text->bytes = 0;
  text->cursor = 0;
  return text;
}

text_t*
text_new (char *str)
{
  text_t *text = allocate(sizeof(text_t));
  text_init(text);
  if (str) text_set(text, str);
  return text;
}

text_t*
text_copy (text_t *text)
{
  return text_new(text_get(text));
}

text_t*
text_take (text_t *text, off_t pos, size_t len)
{
  text_t *new = text_new(NULL);
  if (text->buffer)
  {
    pos = pos >= 0 ? min(text->bytes-1, pos): max(0, (int64_t)text->bytes + pos - 1);

    int available = text->bytes - pos - 1;

    len = len >= 0 ? min(len, available): max(0, available + len);

    new->bytes = len + 1;
    new->buffer = allocate(new->bytes);
    memmove(new->buffer, &text->buffer[pos], len);
    new->buffer[len] = 0;
  }
  return new;
}

void text_home (text_t *text) { text_at(text, 0); }
off_t text_end (text_t *text) { text_at(text, text_count(text)); return text->cursor; }

#define textf(...) ({ text_t *t = text_new(NULL); text_format(t, __VA_ARGS__); t; })

#define FILE_READ (1<<0)
#define FILE_WRITE (1<<1)
#define FILE_CREATE (1<<2)
#define FILE_RESET (1<<3)
#define FILE_BINARY (1<<4)

typedef struct _file_t {
  FILE *handle;
  char *path;
  uint32_t mode;
} file_t;

file_t*
file_open (char *path, uint32_t mode)
{
  file_t *file = allocate(sizeof(file_t));
  memset(file, 0, sizeof(file_t));

  file->path = str_copy(path, strlen(path));
  file->mode = mode;

  struct stat st;
  int exists = stat(file->path, &st) == 0;

  char fmode[5];
  strcpy(fmode, "r");

  if (mode & (FILE_WRITE|FILE_CREATE))
    strcat(fmode, "+");

  if (mode & FILE_RESET || (!exists && mode & FILE_CREATE))
    strcpy(fmode, "w+");

  if (mode & FILE_BINARY)
    strcat(fmode, "b");

  file->handle = fopen(file->path, fmode);

  if (!file->handle)
  {
    free(file->path);
    free(file);
    return NULL;
  }

  return file;
}

file_t*
file_wrap (FILE *handle, uint32_t mode)
{
  file_t *file = allocate(sizeof(file_t));
  memset(file, 0, sizeof(file_t));

  file->handle = handle;
  file->mode = mode;
  return file;
}

void
file_close (file_t *file)
{
  fclose(file->handle);
  free(file->path);
  free(file);
}

void
file_unwrap (file_t *file)
{
  free(file);
}

off_t
file_tell (file_t *file)
{
  return ftello(file->handle);
}

int
file_seek (file_t *file, off_t position)
{
  return fseeko(file->handle, position, SEEK_SET) == 0;
}

int
file_read (file_t *file, void *ptr, size_t bytes)
{
  size_t read = 0;
  for (int i = 0; i < 3; i++)
  {
    read += fread(ptr + read, 1, bytes - read, file->handle);
    if (read == bytes) return 1;
  }
  return 0;
}

int
file_write (file_t *file, void *ptr, size_t bytes)
{
  size_t written = 0;
  for (int i = 0; i < 3; i++)
  {
    written += fwrite(ptr + written, 1, bytes - written, file->handle);
    if (written == bytes) return 1;
  }
  return 0;
}

int
file_print (file_t *file, char *pattern, ...)
{
  va_list args;
  va_start(args, pattern);
  int len = vfprintf(file->handle, pattern, args);
  va_end(args);
  return len;
}

char*
file_read_line (file_t *file)
{
  return str_fgets(file->handle);
}

void*
file_slurp (char *path, size_t *size)
{
  void *ptr = NULL;

  struct stat st;
  if (stat(path, &st) != 0)
    return NULL;

  FILE *file = fopen(path, "r");
  if (!file) return NULL;

  size_t bytes = st.st_size;
  ptr = allocate(bytes + 1);

  size_t read = 0;
  for (int i = 0; i < 3; i++)
  {
    read += fread(ptr + read, 1, bytes - read, file);
    if (read == bytes) break;
  }
  if (read != bytes)
    goto fail;

  if (size)
    *size = bytes;
  ((char*)ptr)[bytes] = 0;
  fclose(file);
  return ptr;

fail:
  fclose(file);
  free(ptr);
  return NULL;
}

int
file_blurt (char *path, void *ptr, size_t bytes)
{
  FILE *file = fopen(path, "w");
  if (!file) return 0;

  size_t written = 0;
  for (int i = 0; i < 3; i++)
  {
    written += fwrite(ptr + written, 1, bytes - written, file);
    if (written == bytes) break;
  }

  fclose(file);

  return written == bytes;
}

struct _array_t;
typedef void (*array_callback)(struct _array_t*);

typedef struct _array_t {
  void **items;
  size_t count;
  size_t width;
  array_callback clear;
} array_t;

typedef struct { off_t index; array_t *array; int l1; } array_each_t;

#define array_each(l,_val_) for ( \
  array_each_t loop = { 0, (l), 0 }; \
    loop.array && loop.index < loop.array->width && !loop.l1 && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = loop.array->items[loop.index]; loop.l1; loop.l1 = !loop.l1)

void
array_clear_free_vals (array_t *array)
{
  array_each(array, void *ptr) free(ptr);
}

void
array_init (array_t *array, size_t width)
{
  array->items = NULL;
  array->count = 0;
  array->width = width;
  array->clear = NULL;
}

void
array_init_items (array_t *array)
{
  array->items = allocate(sizeof(void*) * array->width);
  memset(array->items, 0, sizeof(void*) * array->width);
}

array_t*
array_new (size_t width)
{
  array_t *array = allocate(sizeof(array_t));
  array_init(array, width);
  return array;
}

void
array_clear (array_t *array)
{
  if (array->items)
  {
    if (array->clear)
      array->clear(array);
    free(array->items);
    array->items = NULL;
    array->count = 0;
  }
}

void
array_free (array_t *array)
{
  if (array)
  {
    array_clear(array);
    free(array);
  }
}

void*
array_get (array_t *array, off_t pos)
{
  if (!array->items) return NULL;

  ensure(pos >= 0 && pos < array->width)
    errorf("array_del bounds: %lu", pos);

  return array->items[pos];
}

void
array_set (array_t *array, off_t pos, void *ptr)
{
  if (!array->items)
    array_init_items(array);

  ensure(pos >= 0 && pos < array->width)
    errorf("array_del bounds: %lu", pos);

  array->items[pos] = ptr;
}

struct _vector_t;
typedef void (*vector_callback)(struct _vector_t*);

typedef struct _vector_t {
  void **items;
  size_t count;
  vector_callback clear;
} vector_t;

typedef struct { off_t index; vector_t *vector; int l1; } vector_each_t;

#define vector_each(l,_val_) for ( \
  vector_each_t loop = { 0, (l), 0 }; \
    loop.vector && loop.index < loop.vector->count && !loop.l1 && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = loop.vector->items[loop.index]; loop.l1; loop.l1 = !loop.l1)

void
vector_clear_free_vals (vector_t *vector)
{
  vector_each(vector, void *ptr) free(ptr);
}

void
vector_init (vector_t *vector)
{
  vector->items = NULL;
  vector->count = 0;
  vector->clear = NULL;
}

void
vector_init_items (vector_t *vector)
{
  vector->items = allocate(sizeof(void*));
  vector->items[0] = NULL;
}

vector_t*
vector_new ()
{
  vector_t *vector = allocate(sizeof(vector_t));
  vector_init(vector);
  return vector;
}

void
vector_clear (vector_t *vector)
{
  if (vector->items)
  {
    if (vector->clear)
      vector->clear(vector);
    free(vector->items);
    vector->items = NULL;
    vector->count = 0;
  }
}

void
vector_free (vector_t *vector)
{
  if (vector)
  {
    vector_clear(vector);
    free(vector);
  }
}

void
vector_ins (vector_t *vector, off_t pos, void *ptr)
{
  ensure(pos >= 0 && pos <= vector->count)
    errorf("vector_ins bounds: %lu", pos);

  if (!vector->items)
    vector_init_items(vector);

  vector->items = vector->items ? reallocate(vector->items, sizeof(void*) * (vector->count + 1)): allocate(sizeof(void*));
  memmove(&vector->items[pos+1], &vector->items[pos], (vector->count - pos) * sizeof(void*));
  vector->items[pos] = ptr;
  vector->count++;
}

void*
vector_del (vector_t *vector, off_t pos)
{
  ensure(vector->items && pos >= 0 && pos < vector->count)
    errorf("vector_del bounds: %lu", pos);

  void *ptr = vector->items[pos];
  memmove(&vector->items[pos], &vector->items[pos+1], (vector->count - pos - 1) * sizeof(void*));
  vector->count--;
  return ptr;
}

void*
vector_get (vector_t *vector, off_t pos)
{
  ensure(vector->items && pos >= 0 && pos < vector->count)
    errorf("vector_del bounds: %lu", pos);

  return vector->items[pos];
}

void
vector_set (vector_t *vector, off_t pos, void *ptr)
{
  ensure(pos >= 0 && pos < vector->count)
    errorf("vector_del bounds: %lu", pos);

  if (!vector->items)
    vector_init_items(vector);

  vector->items[pos] = ptr;
}

void
vector_push (vector_t *vector, void *ptr)
{
  vector_ins(vector, vector->count, ptr);
}

void*
vector_pop (vector_t *vector)
{
  return vector_del(vector, vector->count-1);
}

void
vector_shove (vector_t *vector, void *ptr)
{
  vector_ins(vector, 0, ptr);
}

void*
vector_shift (vector_t *vector)
{
  return vector_del(vector, 0);
}

size_t
vector_count (vector_t *vector)
{
  return vector->count;
}

struct _list_t;
typedef void (*list_callback)(struct _list_t*);

typedef struct _list_node_t {
  void *val;
  struct _list_node_t *next, *prev;
} list_node_t;

typedef struct _list_t {
  list_node_t *first;
  list_node_t *last;
  size_t count;
  list_callback clear;
} list_t;

list_node_t*
list_next (list_t *list, list_node_t *node)
{
  return node ? node->next: list->first;
}

typedef struct { off_t index; list_t *list; list_node_t *node; int l1; } list_each_t;

#define list_each(l,_val_) for ( \
  list_each_t loop = { 0, (l), NULL, 0 }; \
    loop.list && !loop.l1 && (loop.node = list_next(loop.list, loop.node)) && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = loop.node->val; loop.l1; loop.l1 = !loop.l1)

list_node_t*
list_find (list_t *list, off_t position)
{
  position = min(list->count, position);
  list_node_t *node = list->first;
  for (off_t i = 0; node && i < position; i++)
    node = node->next;
  return node;
}

void
list_ins (list_t *list, off_t position, void *val)
{
  position = min(list->count, position);

  list_node_t *node = allocate(sizeof(list_node_t));
  node->val  = val;
  node->prev = NULL;
  node->next = NULL;

  if (position == 0)
  {
    if (list->first)
      list->first->prev = node;
    node->next = list->first;
    list->first = node;
    if (!list->last)
      list->last = node;
  }
  else
  if (position == list->count)
  {
    if (list->last)
      list->last->next = node;
    node->prev = list->last;
    list->last = node;
  }
  else
  {
    list_node_t *current = list_find(list, position);
    node->next = current;
    node->prev = current->prev;
    current->prev = node;
  }
  list->count++;
}

int
list_set (list_t *list, off_t position, void *val)
{
  int rc = 0;
  list_node_t *current = list_find(list, position);

  if (current)
  {
    current->val = val;
    rc = 1;
  }
  else
  {
    list_ins(list, position, val);
    rc = 2;
  }
  return rc;
}

void*
list_del (list_t *list, off_t position)
{
  void *val = NULL;
  list_node_t *current = list_find(list, position);

  if (current)
  {
    val = current->val;

    if (current->prev)
      current->prev->next = current->next;
    else
      list->first = current->next;

    if (current->next)
      current->next->prev = current->prev;
    else
      list->last = current->prev;

    free(current);
    list->count--;
  }
  return val;
}

void*
list_get (list_t *list, off_t position)
{
  list_node_t *node = list_find(list, position);
  return node ? node->val: NULL;
}

void
list_init (list_t *list)
{
  memset(list, 0, sizeof(list_t));
}

list_t*
list_new ()
{
  list_t *list = allocate(sizeof(list_t));
  list_init(list);
  return list;
}

void
list_clear (list_t *list)
{
  if (list->clear) list->clear(list);
  while (list->first)
  {
    list_node_t *node = list->first;
    list->first = node->next;
    free(node);
  }
  list->count = 0;
  list->first = NULL;
  list->last  = NULL;
}

void
list_free (list_t *list)
{
  if (list)
  {
    list_clear(list);
    free(list);
  }
}

size_t
list_count (list_t *list)
{
  return list->count;
}

void
list_push (list_t *list, void *val)
{
  list_ins(list, list_count(list), val);
}

void*
list_pop (list_t *list)
{
  return list->count ? list_del(list, list_count(list)-1): NULL;
}

void
list_shove (list_t *list, void *val)
{
  list_ins(list, 0, val);
}

void*
list_shift (list_t *list)
{
  return list->count ? list_del(list, 0): NULL;
}

list_t*
list_scan_skip (char *s, str_cb_ischar cb)
{
  list_t *list = list_new();
  while (s && *s)
  {
    char *start = s;
    char *stop  = start + str_scan(start, cb);
    int length  = stop - start;

    if (length)
    {
      char *substr = allocate(length + 1);
      memmove(substr, start, length);
      substr[length] = 0;
      list_push(list, substr);
    }
    s = stop + str_skip(stop, cb);
  }
  return list;
}

void
list_clear_free (list_t *list)
{
  list_each(list, void *val) free(val);
}

struct _map_t;
typedef void (*map_callback)(struct _map_t*);

typedef int (*map_callback_cmp)(void*, void*);
typedef uint32_t (*map_callback_hash)(void*);

typedef struct _map_node_t {
  void *key;
  void *val;
  uint32_t hash;
} map_node_t;

typedef struct _map_t {
  vector_t *chains;
  map_callback_hash hash;
  map_callback_cmp compare;
  map_callback clear;
  size_t count;
  size_t width;
  size_t depth;
} map_t;

map_node_t*
map_next (map_t *map, map_node_t *node)
{
  if (!map->chains)
    return NULL;

  if (!node)
  {
    for (int i = 0; i < map->width; i++)
      if (map->chains[i].count) return vector_get(&map->chains[i], 0);
    return NULL;
  }

  int chain = node->hash % map->width;
  vector_t *vector = &map->chains[chain];

  vector_each(vector, map_node_t *n)
    if (n == node && loop.index < vector->count-1)
      return vector_get(vector, loop.index+1);

  for (int i = chain+1; i < map->width; i++)
    if (map->chains[i].count) return vector_get(&map->chains[i], 0);
  return NULL;
}

typedef struct { off_t index; map_t *map; map_node_t *node; int l1; int l2; } map_each_t;

#define map_each(l,_key_,_val_) for ( \
  map_each_t loop = { 0, (l), NULL, 0, 0 }; \
    loop.map && !loop.l1 && !loop.l2 && (loop.node = map_next(loop.map, loop.node)) && (loop.l1 = 1) && (loop.l2 = 1); \
    loop.index++ \
  ) \
    for (_key_ = loop.node->key; loop.l1; loop.l1 = !loop.l1) \
      for (_val_ = loop.node->val; loop.l2; loop.l2 = !loop.l2)

typedef struct { off_t index; map_t *map; map_node_t *node; int l1; } map_each_key_t;

#define map_each_key(l,_key_) for ( \
  map_each_key_t loop = { 0, (l), NULL, 0 }; \
    loop.map && !loop.l1 && (loop.node = map_next(loop.map, loop.node)) && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_key_ = loop.node->key; loop.l1; loop.l1 = !loop.l1)

typedef struct { off_t index; map_t *map; map_node_t *node; int l1; } map_each_val_t;

#define map_each_val(l,_val_) for ( \
  map_each_val_t loop = { 0, (l), NULL, 0 }; \
    loop.map && !loop.l1 && (loop.node = map_next(loop.map, loop.node)) && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = loop.node->val; loop.l1; loop.l1 = !loop.l1)

uint32_t
map_str_hash (void *a)
{
  return djb_hash((char*)a);
}

int
map_str_compare (void *a, void *b)
{
  return strcmp((char*)a, (char*)b);
}

uint32_t
map_text_hash (void *a)
{
  return djb_hash(text_get((text_t*)a));
}

int
map_text_compare (void *a, void *b)
{
  return strcmp(text_get((text_t*)a), text_get((text_t*)b));
}

void
map_init (map_t *map, size_t width)
{
  map->chains  = NULL;
  map->hash    = NULL;
  map->compare = NULL;
  map->clear   = NULL;
  map->count   = 0;
  map->width   = width;
  map->depth   = 5;
}

void
map_init_chains (map_t *map)
{
  size_t bytes = sizeof(vector_t) * map->width;
  map->chains = allocate(bytes);
  memset(map->chains, 0, bytes);

  for (off_t i = 0; i < map->width; i++)
  {
    vector_init(&map->chains[i]);
    map->chains[i].clear = vector_clear_free_vals;
  }
}

void
map_resize (map_t *map, size_t width)
{
  map_t tmp;

  map_init(&tmp, width);
  map_init_chains(&tmp);

  tmp.hash = map->hash;
  tmp.compare = map->compare;

  for (off_t i = 0; i < map->width; i++)
  {
    vector_each(&map->chains[i], map_node_t *node)
    {
      int chain = node->hash % tmp.width;
      vector_push(&tmp.chains[chain], node);
    }
    map->chains[i].clear = NULL;
    vector_clear(&map->chains[i]);
  }
  free(map->chains);
  map->chains = tmp.chains;
  map->width  = tmp.width;
}

int
map_set (map_t *map, void *key, void *val)
{
  if (!map->chains)
    map_init_chains(map);

  uint32_t hv = map->hash(key);
  int chain = hv % map->width;

  vector_t *vector = &map->chains[chain];

  off_t pos = 0;
  vector_each(vector, map_node_t *node)
  {
    int cmp = map->compare(node->key, key);
    if (cmp < 0)
      pos = loop.index+1;
    else
    if (cmp > 0)
      break;
    else
    if (cmp == 0)
    {
      node->key = key;
      node->val = val;
      return 2;
    }
  }

  map_node_t *node = allocate(sizeof(map_node_t));
  node->key = key;
  node->val = val;
  node->hash = hv;

  vector_ins(vector, pos, node);
  map->count++;

  if (map->count > map->width * map->depth)
  {
    if (map->width == PRIME_1000)
      map_resize(map, PRIME_10000);
    else
    if (map->width == PRIME_10000)
      map_resize(map, PRIME_100000);
    else
    if (map->width == PRIME_100000)
      map_resize(map, PRIME_1000000);
  }
  return 1;
}

map_node_t*
map_find (map_t *map, void *key)
{
  if (!map->chains) return NULL;

  int chain = map->hash(key) % map->width;
  vector_t *vector = &map->chains[chain];

  vector_each(vector, map_node_t *node)
  {
    int cmp = map->compare(node->key, key);
    if (cmp == 0) return node;
    if (cmp  > 0) break;
  }
  return NULL;
}

void*
map_get (map_t *map, void *key)
{
  map_node_t *node = map_find(map, key);
  return node ? node->val: NULL;
}

int
map_has (map_t *map, void *key)
{
  map_node_t *node = map_find(map, key);
  return node ? 1:0;
}

void*
map_del (map_t *map, void *key)
{
  if (!map->chains) return NULL;

  int chain = map->hash(key) % map->width;
  vector_t *vector = &map->chains[chain];

  vector_each(vector, map_node_t *node)
  {
    int cmp = map->compare(node->key, key);
    if (cmp  > 0) break;

    if (cmp == 0)
    {
      void *ptr = node->val;
      vector_del(vector, loop.index);
      map->count--;
      return ptr;
    }
  }
  return NULL;
}

map_t*
map_new ()
{
  map_t *map = allocate(sizeof(map_t));
  map_init(map, PRIME_1000);
  map->compare = map_str_compare;
  map->hash = map_str_hash;
  return map;
}

void
map_clear (map_t *map)
{
  if (map->chains)
  {
    if (map->clear)
      map->clear(map);

    for (int i = 0; i < map->width; i++)
      vector_clear(&map->chains[i]);

    free(map->chains);
    map->chains = NULL;
    map->count = 0;
  }
}

void
map_free (map_t *map)
{
  if (map)
  {
    map_clear(map);
    free(map);
  }
}

size_t
map_count (map_t *map)
{
  return map->count;
}

void
map_clear_free (map_t *map)
{
  map_each(map, void *key, void *val) { free(key); free(val); }
}

void
map_clear_free_keys (map_t *map)
{
  map_each_key(map, void *key) free(key);
}

void
map_clear_free_vals (map_t *map)
{
  map_each_val(map, void *val) free(val);
}

void
map_clear_text_free (map_t *map)
{
  map_each(map, void *key, void *val) { text_free(key); text_free(val); }
}

void
map_clear_text_free_keys (map_t *map)
{
  map_each_key(map, void *key) text_free(key);
}

void
map_clear_text_free_vals (map_t *map)
{
  map_each_val(map, void *val) text_free(val);
}


#define JSON_OBJECT 1
#define JSON_ARRAY 2
#define JSON_STRING 3
#define JSON_NUMBER 4
#define JSON_INTEGER 5
#define JSON_DOUBLE 6
#define JSON_BOOLEAN 7

typedef struct _json_t {
  int type;
  char *start;
  size_t length;
  struct _json_t *sibling;
  struct _json_t *children;
} json_t;

json_t* json_parse (char *subject);

#define json_is_integer(j) ((j) && (j)->type == JSON_INTEGER)
#define json_is_double(j) ((j) && (j)->type == JSON_DOUBLE)
#define json_is_string(j) ((j) && (j)->type == JSON_STRING)
#define json_is_array(j)  ((j) && (j)->type == JSON_ARRAY)
#define json_is_object(j) ((j) && (j)->type == JSON_OBJECT)
#define json_is_boolean(j) ((j) && (j)->type == JSON_BOOLEAN)

#define json_double(j) strtod((j)->start, NULL)
#define json_integer(j) strtoll((j)->start, NULL, 0)
#define json_boolean(j) (strchr("tT", (j)->start[0]) != NULL)

char*
json_string (json_t *json)
{
  return isalpha(json->start[0]) ? str_copy(json->start, str_skip(json->start, isname)): str_decode(json->start, NULL, STR_ENCODE_DQUOTE);
}

json_t*
json_new ()
{
  json_t *json = allocate(sizeof(json_t));
  memset(json, 0, sizeof(json_t));
  return json;
}

json_t*
json_parse_boolean (char *subject)
{
  json_t *json = json_new();
  json->type   = JSON_BOOLEAN;
  json->start  = subject;
  json->length = str_skip(subject, isalpha);
  return json;
}

json_t*
json_parse_number (char *subject)
{
  char *e1 = NULL;
  char *e2 = NULL;

  strtoll(subject, &e1, 0);
  strtod(subject, &e2);

  json_t *json = json_new();
  json->type   = JSON_NUMBER;
  json->start  = subject;

  if (e1 >= e2)
  {
    json->length = e1 - subject;
    json->type = JSON_INTEGER;
  }
  else
  {
    json->length = e2 - subject;
    json->type = JSON_DOUBLE;
  }
  return json;
}

json_t*
json_parse_string (char *subject)
{
  char *end = NULL;

  if (subject[0] == '"')
  {
    char *str = str_decode(subject, &end, STR_ENCODE_DQUOTE);
    free(str);
  }
  else
  {
    end = subject + str_skip(subject, isname);
  }

  json_t *json = json_new();
  json->type   = JSON_STRING;
  json->start  = subject;
  json->length = end - subject;

  return json;
}

json_t*
json_parse_array (char *subject)
{
  json_t *json = NULL;
  char *start = subject;

  if (*subject++ != '[')
    goto done;

  json = json_new();
  json->type   = JSON_ARRAY;
  json->start  = start;

  json_t *last = NULL;

  while (subject && *subject)
  {
    subject += str_skip(subject, isspace);

    int c = subject[0];

    if (!c)
      break;

    if (c == ']')
    {
      subject++;
      break;
    }

    if (c == ',')
    {
      subject++;
      continue;
    }

    json_t *item = json_parse(subject);

    if (!item || !item->length)
      break;

    subject = item->start + item->length;

    if (last)
    {
      last->sibling = item;
      last = item;
    }
    else
    {
      last = item;
      json->children = item;
    }
  }
  json->length = subject - start;

done:
  return json;
}

json_t*
json_parse_object (char *subject)
{
  json_t *json = NULL;
  char *start = subject;

  if (*subject++ != '{')
    goto done;

  json = json_new();
  json->type   = JSON_OBJECT;
  json->start  = start;

  json_t *last = NULL;

  while (subject && *subject)
  {
    subject += str_skip(subject, isspace);

    int c = *subject;

    if (!c)
      break;

    if (c == '}')
    {
      subject++;
      break;
    }

    if (c == ',' || c == ':')
    {
      subject++;
      continue;
    }

    json_t *item = json_parse(subject);

    if (!item || !item->length)
      break;

    subject = item->start + item->length;

    if (last)
    {
      last->sibling = item;
      last = item;
    }
    else
    {
      last = item;
      json->children = item;
    }
  }
  json->length = subject - start;

done:
  return json;
}

json_t*
json_parse (char *subject)
{
  subject += str_skip(subject, isspace);

  int c = subject[0];
  json_t *child = NULL;

  if (c == '{')
    child = json_parse_object(subject);
  else if (c == '[')
    child = json_parse_array(subject);
  else if (strchr("tTfF", c))
    child = json_parse_boolean(subject);
  else if (c == '"' || isalpha(c))
    child = json_parse_string(subject);
  else
    child = json_parse_number(subject);
  return child;
}

void
json_free (json_t *json)
{
  while (json && json->children)
  {
    json_t *item = json->children;
    json->children = item->sibling;
    json_free(item);
  }
  free(json);
}

json_t*
json_object_get (json_t *json, char *name)
{
  if (!json || json->type != JSON_OBJECT)
    return NULL;

  for (json_t *key = json->children; key && key->sibling; key = key->sibling)
  {
    if (key->type == JSON_STRING && key->sibling)
    {
      char *str = json_string(key);
      int found = !strcmp(str, name);
      free(str);
      if (found) return key->sibling;
    }
    key = key->sibling;
  }
  return NULL;
}

json_t*
json_array_get (json_t *json, int index)
{
  if (!json || json->type != JSON_ARRAY)
    return NULL;

  int i = 0;
  for (json_t *item = json->children; item; item = item->sibling)
  {
    if (i == index) return item;
    i++;
  }
  return NULL;
}

typedef struct _pool_header_t {
  size_t osize;
  size_t psize;
  size_t pstep;
  off_t pnext;
  off_t pfree;
} pool_header_t;

typedef struct _pool_t {
  char *name;
  pool_header_t *head;
  int fd;
  void *map;
  unsigned char *bitmap;
} pool_t;

void
pool_bitmap (pool_t *pool)
{
  free(pool->bitmap);

  pool->bitmap = allocate(pool->head->psize / 8 + 1);
  memset(pool->bitmap, 0, pool->head->psize / 8 + 1);

  for (off_t pos = pool->head->pfree; pos; pos = *((off_t*)(pool->map + pos)))
    pool->bitmap[pos / 8] |= 1 << (pos % 8);
}

void
pool_open (pool_t *pool, char *name, size_t osize, size_t pstep)
{
  osize = max(osize, sizeof(off_t*));

  pool->map  = NULL;
  pool->head = NULL;
  pool->fd   = 0;
  pool->name = strdup(name);
  pool->bitmap = NULL;

  struct stat st;

  if (stat(pool->name, &st) == 0)
  {
    ensure((st.st_size - sizeof(pool_header_t)) % osize == 0)
      errorf("pool file exists with invalid size: %s", pool->name);

    pool->fd = open(pool->name, O_RDWR);

    ensure(pool->fd >= 0)
      errorf("cannot reopen pool: %s", pool->name);

    pool->map = mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, pool->fd, 0);

    ensure(pool->map && pool->map != MAP_FAILED)
      errorf("cannot mmap pool: %s", pool->name);

    pool->head = pool->map;

    ensure(pool->head->osize == osize
      && pool->head->pstep == pstep
      && pool->head->pnext <= pool->head->psize
      && pool->head->pfree <= pool->head->psize)
        errorf("pool head mismatch: %s", pool->name);
  }
  else
  {
    pool->fd = open(pool->name, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);

    ensure(pool->fd >= 0)
      errorf("cannot create pool: %s", pool->name);

    size_t bytes = 1000 * osize + sizeof(pool_header_t);

    void *ptr = allocate(bytes);
    memset(ptr, 0, bytes);

    ensure(write(pool->fd, ptr, bytes) == bytes)
      errorf("cannot write pool: %s", pool->name);

    free(ptr);

    pool->map = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED, pool->fd, 0);

    ensure(pool->map && pool->map != MAP_FAILED)
      errorf("cannot mmap pool: %s", pool->name);

    pool->head = pool->map;
    pool->head->pnext = sizeof(pool_header_t);
    pool->head->osize = osize;
    pool->head->pstep = pstep;
    pool->head->psize = bytes;
  }

  pool_bitmap(pool);
}

void
pool_close (pool_t *pool)
{
  ensure(munmap(pool->map, pool->head->psize) == 0)
    errorf("cannot unmap pool: %s", pool->name);

  ensure(close(pool->fd) == 0)
    errorf("cannot close pool: %s", pool->name);

  pool->fd = 0;
  free(pool->name);
  free(pool->bitmap);
  pool->name = NULL;
  pool->head = NULL;
  pool->map = NULL;
}

void*
pool_read (pool_t *pool, off_t position, void *ptr)
{
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  if (ptr)
  {
    memmove(ptr, pool->map + position, pool->head->osize);
    return ptr;
  }

  return pool->map + position;
}

void
pool_write (pool_t *pool, off_t position, void *ptr)
{
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  if (ptr && ptr != pool->map + position)
    memmove(pool->map + position, ptr, pool->head->osize);
}

off_t
pool_alloc (pool_t *pool)
{
  if (pool->head->pfree)
  {
    off_t position = pool->head->pfree;
    pool->head->pfree = *((off_t*)(pool->map + pool->head->pfree));
    pool->bitmap[position / 8] &= ~(1 << (position % 8));
    return position;
  }

  if (pool->head->pnext == pool->head->psize)
  {
    size_t bytes = pool->head->pstep * pool->head->osize;
    size_t psize = pool->head->psize;

    ensure(munmap(pool->map, psize) == 0)
      errorf("cannot unmap pool: %s", pool->name);

    void *ptr = allocate(bytes);
    memset(ptr, 0, bytes);

    ensure(lseek(pool->fd, psize, SEEK_SET))
      errorf("cannot seek pool: %s", pool->name);

    ensure(write(pool->fd, ptr, bytes) == bytes)
      errorf("cannot write pool: %s", pool->name);

    free(ptr);

    pool->map = mmap(NULL, psize + bytes, PROT_READ|PROT_WRITE, MAP_SHARED, pool->fd, 0);

    ensure(pool->map && pool->map != MAP_FAILED)
      errorf("cannot mmap pool: %s", pool->name);

    pool->head = pool->map;
    pool->head->psize = psize + bytes;
    pool_bitmap(pool);
  }

  off_t position = pool->head->pnext;
  memset(pool->map + position, 0, pool->head->osize);
  pool->head->pnext += pool->head->osize;
  return position;
}

void
pool_free (pool_t *pool, off_t position)
{
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  *((off_t*)(pool->map + position)) = pool->head->pfree;
  pool->head->pfree = position;

  pool->bitmap[position / 8] |= 1 << (position % 8);
}

void
pool_sync (pool_t *pool)
{
  ensure(msync(pool->map, pool->head->pnext, MS_SYNC) == 0)
    errorf("pool sync failed: %s", pool->name);
}

int
pool_is_free (pool_t *pool, off_t position)
{
  return position >= pool->head->pnext || pool->bitmap[position / 8] & 1 << (position % 8) ? 1:0;
}

off_t
pool_next (pool_t *pool, off_t position)
{
  position += position ? pool->head->osize: sizeof(pool_header_t);

  while (position < pool->head->psize && pool_is_free(pool, position))
    position += pool->head->osize;

  return (position < pool->head->psize) ? position: 0;
}

typedef struct { off_t index; pool_t *pool; off_t pos; int l1; } pool_each_t;

#define pool_each(l,_val_) for ( \
  pool_each_t loop = { 0, (l), 0, 0 }; \
    !loop.l1 && (loop.pos = pool_next(loop.pool, loop.pos)) && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = pool_read(loop.pool, loop.pos, NULL); loop.l1; loop.l1 = !loop.l1)

void*
pool_read_chunk (pool_t *pool, off_t position, size_t bytes, void *ptr)
{
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize - bytes)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  if (ptr)
  {
    memmove(ptr, pool->map + position, bytes);
    return ptr;
  }

  return pool->map + position;
}

void
pool_write_chunk (pool_t *pool, off_t position, size_t bytes, void *ptr)
{
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize - bytes)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  if (ptr && ptr != pool->map + position)
    memmove(pool->map + position, ptr, bytes);
}

off_t
pool_alloc_chunk (pool_t *pool, size_t bytes)
{
  if (bytes <= pool->head->osize)
    return pool_alloc(pool);

  size_t slots = (bytes / pool->head->osize) + (bytes % pool->head->osize ? 1:0);

  for (off_t pos = pool->head->pfree; pos; pos = *((off_t*)(pool->map + pos)))
  {
    int found = 1;
    for (int i = 0; found && i < slots; i++)
      found = pool_is_free(pool, pos + (i * pool->head->osize));

    if (found)
    {
      for (int i = 0; i < slots; i++)
        pool->bitmap[(pos + (i * pool->head->osize)) / 8] &= ~(1 << ((pos + (i * pool->head->osize)) % 8));
      return pos;
    }
  }

  off_t pfree = pool->head->pfree;
  pool->head->pfree = 0;

  off_t pos = pool->head->pnext;

  for (int i = 0; i < slots; i++)
    pool_alloc(pool);

  pool->head->pfree = pfree;
  return pos;
}

void
pool_free_chunk (pool_t *pool, off_t pos, size_t bytes)
{
  size_t slots = bytes / pool->head->osize + (bytes % pool->head->osize ? 1:0);

  for (int i = 0; i < slots; i++)
    pool_free(pool, pos + (i * pool->head->osize));
}

#ifdef TOOLBELT_DB

#include <postgresql/libpq-fe.h>

typedef struct _db_t {
  PGconn *conn;
} db_t;

typedef struct _dbr_t {
  db_t *db;
  PGresult *res;
  size_t affected;
  size_t selected;
  size_t fetched;
  size_t fields;
} dbr_t;

void
dbr_clear (dbr_t *dbr)
{
  PQclear(dbr->res);
}

void
dbr_free (dbr_t *dbr)
{
  dbr_clear(dbr);
  free(dbr);
}

size_t
dbr_selected (dbr_t *dbr)
{
  return dbr->selected;
}

map_t*
dbr_fetch_map (dbr_t *dbr)
{
  if (dbr->fetched == dbr->selected)
    return NULL;

  map_t *row = map_new();

  for (size_t i = 0; i < dbr->fields; i++)
  {
    char *key = PQfname(dbr->res, i);
    char *val = PQgetisnull(dbr->res, dbr->fetched, i) ? NULL: PQgetvalue(dbr->res, dbr->fetched, i);
    map_set(row, key, val);
  }
  dbr->fetched++;
  return row;
}

array_t*
dbr_fetch_array (dbr_t *dbr)
{
  if (dbr->fetched == dbr->selected)
    return NULL;

  array_t *row = array_new(dbr->fields);

  for (size_t i = 0; i < dbr->fields; i++)
  {
    char *val = PQgetisnull(dbr->res, dbr->fetched, i) ? NULL: PQgetvalue(dbr->res, dbr->fetched, i);
    array_set(row, i, val);
  }
  dbr->fetched++;
  return row;
}

dbr_t*
db_query (db_t *db, const char *query)
{
  dbr_t *dbr = allocate(sizeof(dbr_t));
  dbr->res = PQexec(db->conn, query);

  ExecStatusType rs = PQresultStatus(dbr->res);

  if (rs != PGRES_TUPLES_OK && rs != PGRES_SINGLE_TUPLE && rs != PGRES_COMMAND_OK)
  {
    errorf("PostgresSQL query failed: %s", PQresultErrorMessage(dbr->res));
    dbr_free(dbr);
    return NULL;
  }

  dbr->affected = strtoll(PQcmdTuples(dbr->res), NULL, 0);
  dbr->selected = PQntuples(dbr->res);
  dbr->fetched  = 0;
  dbr->fields   = PQnfields(dbr->res);
  return dbr;
}

char*
db_quote_field (db_t *db, const char *field)
{
  return strchr(field, '.') ? strf("%s", field) : strf("\"%s\"", field);
}

char*
db_quote_value (db_t *db, const char *value)
{
  return str_encode((char*)value, STR_ENCODE_SQL);
}

void
db_begin (db_t *db)
{
  dbr_free(db_query(db, "begin"));
}

void
db_commit (db_t *db)
{
  dbr_free(db_query(db, "commit"));
}

void
db_rollback (db_t *db)
{
  dbr_free(db_query(db, "rollback"));
}

void
db_command (db_t *db, const char *query)
{
  dbr_free(db_query(db, query));
}

int
db_connect (db_t *db, const char *dbhost, const char *dbname, const char *dbuser, const char *dbpass)
{
  text_t *info = textf("host=%s dbname=%s user=%s password=%s",
    dbhost, dbname, dbuser, dbpass
  );

  db->conn = PQconnectdb(text_get(info));

  text_free(info);

  if (PQstatus(db->conn) != CONNECTION_OK)
  {
    errorf("PostgresSQL connect failed: %s", PQerrorMessage(db->conn));
    return 0;
  }
  return 1;
}

void
db_close (db_t *db)
{
  PQfinish(db->conn);
}

#endif

#ifdef TOOLBELT_THREAD

#include <pthread.h>

typedef int (*thread_main)(void*);

typedef struct _channel_t {
  pthread_mutex_t mutex;
  list_t *queue;
  list_t *readers;
  size_t handled;
} channel_t;

typedef struct _thread_t {
  pthread_t pthread;
  pthread_mutex_t mutex;
  thread_main main;
  void *payload;
  off_t id;
  int started;
  int stopped;
  int joined;
  int error;
  int waiting;
  pthread_cond_t cond;
  channel_t *channel;
  void *ptr;
} thread_t;

static pthread_key_t _key_self;
#define self ((thread_t*)pthread_getspecific(_key_self))

pthread_mutex_t all_threads_mutex;
vector_t *all_threads;

int
thread_join (thread_t *thread)
{
  int ok = 1;

  if (thread->started && !thread->joined)
  {
    pthread_mutex_unlock(&thread->mutex);
    int rc = pthread_join(thread->pthread, NULL);
    pthread_mutex_lock(&thread->mutex);
    if (rc == 0)
      thread->joined = 1;
    else
    {
      errorf("thread_join: %d", rc);
      ok = 0;
    }
  }
  return ok;
}

thread_t*
thread_new ()
{
  pthread_mutex_lock(&all_threads_mutex);

  vector_each(all_threads, thread_t *t)
  {
    pthread_mutex_lock(&t->mutex);
    if (t->stopped)
    {
      thread_join(t);
      pthread_mutex_unlock(&all_threads_mutex);
      return t;
    }
    pthread_mutex_unlock(&t->mutex);
  }

  thread_t *thread = allocate(sizeof(thread_t));
  memset(thread, 0, sizeof(thread_t));
  pthread_mutex_init(&thread->mutex, NULL);
  pthread_cond_init(&thread->cond, NULL);

  thread->id = vector_count(all_threads);
  vector_push(all_threads, thread);

  pthread_mutex_unlock(&all_threads_mutex);
  return thread;
}

void*
thread_run (void *ptr)
{
  thread_t *thread = (thread_t*)ptr;
  pthread_setspecific(_key_self, thread);

  int error = thread->main(thread->payload);

  pthread_mutex_lock(&thread->mutex);
  thread->error = error;
  thread->stopped = 1;
  pthread_mutex_unlock(&thread->mutex);
  return NULL;
}

int
thread_start (thread_t *thread, thread_main main, void *payload)
{
  pthread_mutex_lock(&thread->mutex);

  thread_join(thread);

  thread->main    = main;
  thread->payload = payload;
  thread->started = 1;
  thread->stopped = 0;
  thread->joined  = 0;
  thread->waiting = 0;

  pthread_mutex_unlock(&thread->mutex);

  int rc = pthread_create(&thread->pthread, NULL, thread_run, thread);

  if (rc)
  {
    errorf("thread_start (create): %d", rc);
    return 0;
  }
  return 1;
}

int
thread_wait (thread_t *thread)
{
  pthread_mutex_lock(&thread->mutex);
  thread_join(thread);
  int rc = thread->error;
  pthread_mutex_unlock(&thread->mutex);
  return rc;
}

void
thread_free (thread_t *thread)
{
  pthread_mutex_destroy(&thread->mutex);
  pthread_cond_destroy(&thread->cond);
  free(thread);
}

void
multithreaded ()
{
  pthread_key_create(&_key_self, NULL);
  pthread_mutex_init(&all_threads_mutex, NULL);
  all_threads = vector_new();
}

void
singlethreaded ()
{
  pthread_mutex_lock(&all_threads_mutex);

  vector_each(all_threads, thread_t *thread)
  {
    pthread_mutex_lock(&thread->mutex);
    thread_join(thread);
    thread_free(thread);
  }
  vector_free(all_threads);
  all_threads = NULL;

  int rc = pthread_key_delete(_key_self);

  if (rc != 0)
    errorf("singlethreaded (key delete): %d", rc);

  pthread_mutex_destroy(&all_threads_mutex);
}

channel_t*
channel_new ()
{
  channel_t *channel = allocate(sizeof(channel_t));
  pthread_mutex_init(&channel->mutex, NULL);
  channel->queue = list_new();
  channel->readers = list_new();
  channel->handled = 0;
  return channel;
}

void
channel_free (channel_t *channel)
{
  pthread_mutex_lock(&channel->mutex);
  pthread_mutex_destroy(&channel->mutex);
  list_free(channel->queue);
  list_free(channel->readers);
  free(channel);
}

void
channel_write (channel_t *channel, void *ptr)
{
  pthread_mutex_lock(&channel->mutex);
  channel->handled++;

  int dispatched = 0;

  while (!dispatched && list_count(channel->readers))
  {
    list_each(channel->readers, thread_t *thread)
    {
      pthread_mutex_lock(&thread->mutex);

      if (thread->waiting)
      {
        thread->channel = channel;
        thread->ptr     = ptr;
        thread->waiting = 0;
        list_del(channel->readers, loop.index);
        pthread_cond_signal(&thread->cond);
        pthread_mutex_unlock(&thread->mutex);
        dispatched = 1;
        break;
      }
      pthread_mutex_unlock(&thread->mutex);
    }
    if (!dispatched)
    {
      pthread_mutex_unlock(&channel->mutex);
      usleep(1);
      pthread_mutex_lock(&channel->mutex);
    }
  }
  if (!dispatched)
  {
    list_push(channel->queue, ptr);
  }
  pthread_mutex_unlock(&channel->mutex);
}

void
channel_broadcast (channel_t *channel, void *ptr)
{
  pthread_mutex_lock(&channel->mutex);

  list_each(channel->readers, thread_t *thread)
  {
    pthread_mutex_lock(&thread->mutex);
    if (thread->waiting)
    {
      thread->channel = channel;
      thread->ptr     = ptr;
      thread->waiting = 0;
      pthread_cond_signal(&thread->cond);
    }
    pthread_mutex_unlock(&thread->mutex);
  }
  list_clear(channel->readers);
  pthread_mutex_unlock(&channel->mutex);
}

void*
channel_try_read (channel_t *channel)
{
  void *ptr = NULL;
  if (pthread_mutex_trylock(&channel->mutex) == 0)
  {
    if (list_count(channel->queue))
      ptr = list_shift(channel->queue);
    pthread_mutex_unlock(&channel->mutex);
  }
  return ptr;
}

list_t*
channel_consume (channel_t *channel)
{
  list_t *consume = NULL;
  pthread_mutex_lock(&channel->mutex);
  if (list_count(channel->queue))
  {
    consume = channel->queue;
    channel->queue = list_new();
  }
  pthread_mutex_unlock(&channel->mutex);
  return consume;
}

void*
channel_multi_read (channel_t **selected, list_t *channels)
{
  void *ptr = NULL;
  channel_t *from = NULL;

  int waiting_channels = 0;
  list_each(channels, channel_t *channel)
  {
    pthread_mutex_lock(&channel->mutex);
    if (list_count(channel->queue))
    {
      ptr = list_shift(channel->queue);
      from = channel;
    }
    else
    {
      list_push(channel->readers, self);
      waiting_channels++;
    }
    pthread_mutex_unlock(&channel->mutex);
    if (ptr) break;
  }

  if (!ptr)
  {
    pthread_mutex_lock(&self->mutex);
    self->waiting = 1;
    pthread_cond_wait(&self->cond, &self->mutex);
    pthread_mutex_unlock(&self->mutex);
    ptr = self->ptr;
    from = self->channel;
  }

  list_each(channels, channel_t *channel)
  {
    if (loop.index == waiting_channels) break;
    if (channel == from) continue;

    pthread_mutex_lock(&channel->mutex);
    list_each(channel->readers, thread_t *thread)
    {
      if (thread == self)
      {
        list_del(channel->readers, loop.index);
        break;
      }
    }
    pthread_mutex_unlock(&channel->mutex);
  }

  if (selected) *selected = from;
  return ptr;
}

void*
channel_select (channel_t **selected, int n, ...)
{
  va_list args;
  va_start(args, n);

  list_t *channels = list_new();

  for (int i = 0; i < n; i++)
    list_push(channels, va_arg(args, channel_t*));

  va_end(args);

  void *ptr = channel_multi_read(selected, channels);

  list_free(channels);

  return ptr;
}

void*
channel_read (channel_t *channel)
{
  return channel_select(NULL, 1, channel);
}

size_t
channel_backlog (channel_t *channel)
{
  pthread_mutex_lock(&channel->mutex);
  size_t backlog = list_count(channel->queue);
  pthread_mutex_unlock(&channel->mutex);
  return backlog;
}

size_t
channel_readers (channel_t *channel)
{
  pthread_mutex_lock(&channel->mutex);
  size_t readers = list_count(channel->readers);
  pthread_mutex_unlock(&channel->mutex);
  return readers;
}

size_t
channel_handled (channel_t *channel)
{
  pthread_mutex_lock(&channel->mutex);
  size_t handled = channel->handled;
  pthread_mutex_unlock(&channel->mutex);
  return handled;
}

#endif

#include <sys/wait.h>

#define PIPE_READ 0
#define PIPE_WRITE 1

// execute sub-process and connect its stdin=infp and stdout=outfp
pid_t
exec_cmd_io(const char *command, int *infp, int *outfp, int *errfp)
{
  int p_stdin[2], p_stdout[2], p_stderr[2];

  if (pipe(p_stdin) != 0 || pipe(p_stdout) != 0 || pipe(p_stderr) != 0)
    return -1;

  pid_t pid = fork();

  if (pid < 0)
    return pid;

  if (pid == 0)
  {
    close(p_stdin[PIPE_WRITE]);
    dup2(p_stdin[PIPE_READ], STDIN_FILENO);

    close(p_stdout[PIPE_READ]);
    dup2(p_stdout[PIPE_WRITE], STDOUT_FILENO);

    close(p_stderr[PIPE_READ]);
    dup2(p_stderr[PIPE_WRITE], STDERR_FILENO);

    execlp("/bin/sh", "sh", "-c", command, NULL);
    exit(EXIT_FAILURE);
  }

  if (infp == NULL)
    close(p_stdin[PIPE_WRITE]);
  else
    *infp = p_stdin[PIPE_WRITE];

  if (outfp == NULL)
    close(p_stdout[PIPE_READ]);
  else
    *outfp = p_stdout[PIPE_READ];

  if (errfp == NULL)
    close(p_stderr[PIPE_READ]);
  else
    *errfp = p_stderr[PIPE_READ];

  close(p_stdin[PIPE_READ]);
  close(p_stdout[PIPE_WRITE]);
  close(p_stderr[PIPE_WRITE]);

  return pid;
}

int
command (const char *cmd, const char *data, char **output, char **errput)
{
  int status = EXIT_SUCCESS;
  int in, out, err;

  pid_t pid = exec_cmd_io(cmd, &in, &out, &err);

  if (pid <= 0)
  {
    status = EXIT_FAILURE;
    goto done;
  }

  if (data && write(in, data, strlen(data)) != strlen(data))
  {
    status = EXIT_FAILURE;
    close(in);
    close(out);
    close(err);
    kill(pid, SIGTERM);
    goto done;
  }

  close(in);

  int outlen = 0;
  char *outres = malloc(1024);
  int errlen = 0;
  char *errres = malloc(1024);
  for (;;)
  {
    int rc = read(out, outres+outlen, 1023);
    if (rc > 0) outlen += rc;
    if (rc < 1023) break;
    outres = realloc(outres, outlen+1024);

    rc = read(err, errres+errlen, 1023);
    if (rc > 0) errlen += rc;
    if (rc < 1023) break;
    errres = realloc(errres, errlen+1024);
  }
  outres[outlen] = 0;
  errres[errlen] = 0;

  if (output) *output = outres; else free(outres);
  if (errput) *errput = errres; else free(errres);

  close(out);
  close(err);

  waitpid(pid, &status, 0);

done:
  return status;
}
