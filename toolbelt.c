#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <libgen.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PRIME_1000 997
#define PRIME_10000 9973
#define PRIME_100000 99991
#define PRIME_1000000 999983

#define ensure(x) for ( ; !(x) ; exit(EXIT_FAILURE) )
#define errorf(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while(0)
#define unless(c) if (!(c))

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
mfgets (FILE *file)
{
  size_t bytes = 100;
  char *line = allocate(bytes+1);
  line[0] = 0;

  while (fgets(line + bytes - 100, 101, file) && !strchr(line + bytes - 100, '\n'))
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
mprintf (char *pattern, ...)
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

#define str_each(l,_val_) for ( \
  struct { int index; char *subject; int l1; } loop = { 0, (l), 0 }; \
    !loop.l1 && loop.subject[loop.index] && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = loop.subject[loop.index]; loop.l1; loop.l1 = !loop.l1) 

typedef int (*str_cb_ischar)(int);

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

int
str_count (char *str, str_cb_ischar cb)
{
  int count = 0;
  str_each(str, char c)
    if (cb(c)) count++;
  return count;
}

int
str_strip (char *s, str_cb_ischar cb)
{
  int count = 0;
  while (s && *s)
  {
    if (cb(*s))
    {
      memmove(s, s+1, strlen(s+1)+1);
      count++;
    }
    s++;
  }
  return count;
}

char*
str_copy (char *s, size_t length)
{
  char *a = allocate(length+1);
  memmove(a, s, length);
  a[length] = 0;
  return a;
}

int
str_is_tsv (char *line, int cols)
{
  int length = line ? strlen(line): 0;
  return line && length >= cols && str_count(line, istab) == cols-1;
}

#define STR_ENCODE_HEX 1
#define STR_ENCODE_SQL 2
#define STR_ENCODE_DQUOTE 3

char*
str_encode (char *s, int format)
{
  char *result = NULL;

  static const char* hex256[256] = {
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
    result = mprintf("\"");
    char *change = NULL;

    str_each(s, char c)
    {
           if (c == 0x07) change = mprintf("%s\\a", result);
      else if (c == 0x08) change = mprintf("%s\\b", result);
      else if (c == 0x0c) change = mprintf("%s\\f", result);
      else if (c == 0x0a) change = mprintf("%s\\n", result);
      else if (c == 0x0d) change = mprintf("%s\\r", result);
      else if (c == 0x09) change = mprintf("%s\\t", result);
      else if (c == 0x0B) change = mprintf("%s\\v", result);
      else if (c == '\\') change = mprintf("%s\\\\", result);
      else if (c ==  '"') change = mprintf("%s\\\"", result);
      else                change = mprintf("%s%c", result, c);

      free(result);
      result = change;
    }
    change = mprintf("%s\"", result);
    free(result);
    result = change;
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

struct _list_t;
typedef void (*list_callback)(struct _list_t*);

typedef struct _list_node_t {
  void *val;
  struct _list_node_t *next;
} list_node_t;

typedef struct _list_t {
  list_node_t *nodes;
  size_t count;
  list_callback empty;
} list_t;

list_node_t* list_next(list_t*, list_node_t*);

#define list_each(l,_val_) for ( \
  struct { int index; list_t *list; list_node_t *node; int l1; } loop = { 0, (l), NULL, 0 }; \
    !loop.l1 && (loop.node = list_next(loop.list, loop.node)) && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = loop.node->val; loop.l1; loop.l1 = !loop.l1) 

void
list_ins (list_t *list, off_t position, void *val)
{
  list_node_t **prev = &list->nodes;
  for (
    off_t i = 0;
    *prev && i < position;
    prev = &((*prev)->next), i++
  );
  list_node_t *node = allocate(sizeof(list_node_t));
  node->val = val;
  node->next = *prev;
  *prev = node;
  list->count++;
}

int
list_set (list_t *list, off_t position, void *val)
{
  int rc = 1;
  list_node_t *node = list->nodes;
  for (
    off_t i = 0;
    node && i < position;
    node = node->next, i++
  );
  if (!node)
  {
    list_ins(list, position, val);
    rc = 2;
  }
  node->val = val;
  return rc;
}

void*
list_del (list_t *list, off_t position)
{
  void *val = NULL;
  list_node_t **prev = &list->nodes;
  for (
    off_t i = 0;
    *prev && i < position;
    prev = &((*prev)->next), i++
  );
  if (*prev)
  {
    list_node_t *node = *prev;
    val = node->val;
    *prev = node->next;
    free(node);
    list->count--;
  }
  return val;
}

void*
list_get (list_t *list, off_t position)
{
  list_each(list, void *val)
    if (loop.index == position)
      return val;
  return NULL;
}

list_node_t*
list_next (list_t *list, list_node_t *node)
{
  if (!node)
    return list->nodes;

  if (node)
    return node->next;

  return NULL;
}

void
list_init (list_t *list)
{
  memset(list, 0, sizeof(list_t));
}

list_t*
list_create ()
{
  list_t *list = allocate(sizeof(list_t));
  list_init(list);
  return list;
}

void
list_empty (list_t *list)
{
  if (list->empty) list->empty(list);
  while (list->nodes)
  {
    list_node_t *node = list->nodes;
    list->nodes = node->next;
    free(node);
  }
  list->count = 0;
}

void
list_free (list_t *list)
{
  list_empty(list);
  free(list);
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
  list_t *list = list_create();
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
list_empty_free (list_t *list)
{
  list_each(list, void *val) free(val);
}

struct _dict_t;
typedef void (*dict_callback)(struct _dict_t*);

typedef int (*dict_callback_cmp)(void*, void*);
typedef uint32_t (*dict_callback_hash)(void*);

typedef struct _dict_node_t {
  void *key;
  void *val;
  uint32_t hash;
  struct _dict_node_t *next;
} dict_node_t;

typedef struct _dict_t {
  dict_node_t **chains;
  dict_callback_hash hash;
  dict_callback_cmp compare;
  dict_callback empty;
  size_t count;
  size_t width;
  size_t depth;
} dict_t;

dict_node_t* dict_next(dict_t*, dict_node_t*);

#define dict_each(l,_key_,_val_) for ( \
  struct { int index; dict_t *dict; dict_node_t *node; int l1; int l2; } loop = { 0, (l), NULL, 0, 0 }; \
    !loop.l1 && !loop.l2 && (loop.node = dict_next(loop.dict, loop.node)) && (loop.l1 = 1) && (loop.l2 = 1); \
    loop.index++ \
  ) \
    for (_key_ = loop.node->key; loop.l1; loop.l1 = !loop.l1) \
      for (_val_ = loop.node->val; loop.l2; loop.l2 = !loop.l2)

#define dict_each_key(l,_key_) for ( \
  struct { int index; dict_t *dict; dict_node_t *node; int l1; } loop = { 0, (l), NULL, 0 }; \
    !loop.l1 && (loop.node = dict_next(loop.dict, loop.node)) && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_key_ = loop.node->key; loop.l1; loop.l1 = !loop.l1) 

#define dict_each_val(l,_val_) for ( \
  struct { int index; dict_t *dict; dict_node_t *node; int l1; } loop = { 0, (l), NULL, 0 }; \
    !loop.l1 && (loop.node = dict_next(loop.dict, loop.node)) && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = loop.node->val; loop.l1; loop.l1 = !loop.l1) 

uint32_t
dict_str_hash (void *a)
{
  return djb_hash((char*)a);
}

int
dict_str_compare (void *a, void *b)
{
  return strcmp((char*)a, (char*)b);
}

void
dict_init (dict_t *dict, size_t width)
{
  memset(dict, 0, sizeof(dict_t));
  dict->width = width;
  dict->depth = 5;

  size_t bytes = sizeof(dict_node_t*) * dict->width;
  dict->chains = allocate(bytes);
  memset(dict->chains, 0, bytes);
}

void
dict_resize (dict_t *dict, size_t width)
{
  dict_t tmp;
  dict_init(&tmp, width);
  tmp.hash = dict->hash;
  tmp.compare = dict->compare;

  for (off_t i = 0; i < dict->width; i++)
  {
    while (dict->chains[i])
    {
      dict_node_t *node = dict->chains[i];
      dict->chains[i] = node->next;

      node->hash = tmp.hash(node->key);
      int chain = node->hash % tmp.width;
      node->next = tmp.chains[chain];
      tmp.chains[chain] = node;
    }
  }
  free(dict->chains);
  dict->chains = tmp.chains;
  dict->width  = tmp.width;
}

int
dict_set (dict_t *dict, void *key, void *val)
{
  int rc = 1;
  uint32_t hv = dict->hash(key);
  int chain = hv % dict->width;

  dict_node_t *node = dict->chains[chain];
  while (node && dict->compare(node->key, key))
    node = node->next;

  if (!node)
  {
    node = allocate(sizeof(dict_node_t));
    node->hash = hv;
    node->next = dict->chains[chain];
    dict->chains[chain] = node;
    dict->count++;
    rc = 2;
  }
  node->key = key;
  node->val = val;

  if (dict->count > dict->width * dict->depth)
  {
    if (dict->width == PRIME_1000)
      dict_resize(dict, PRIME_10000);
    else
    if (dict->width == PRIME_10000)
      dict_resize(dict, PRIME_100000);
    else
    if (dict->width == PRIME_100000)
      dict_resize(dict, PRIME_1000000);
  }
  return rc;
}

dict_node_t*
dict_find (dict_t *dict, void *key)
{
  int chain = dict->hash(key) % dict->width;

  dict_node_t *node = dict->chains[chain];
  while (node && dict->compare(node->key, key))
    node = node->next;

  return node;
}

void*
dict_get (dict_t *dict, void *key)
{
  dict_node_t *node = dict_find(dict, key);
  return node ? node->val: NULL;
}

int
dict_has (dict_t *dict, void *key)
{
  dict_node_t *node = dict_find(dict, key);
  return node ? 1:0;
}

void*
dict_del (dict_t *dict, void *key)
{
  int chain = dict->hash(key) % dict->width;

  dict_node_t **prev = &dict->chains[chain];
  while (*prev && dict->compare((*prev)->key, key))
    prev = &((*prev)->next);

  if (*prev)
  {
    dict_node_t *node = *prev;
    *prev = node->next;
    void *val = node->val;
    free(node);
    dict->count--;
    return val;
  }
  return NULL;
}

dict_node_t*
dict_next (dict_t *dict, dict_node_t *node)
{
  if (!node)
  {
    for (int i = 0; i < dict->width; i++)
      if (dict->chains[i]) return dict->chains[i];
    return NULL;
  }

  if (node->next)
    return node->next;

  for (int i = (node->hash % dict->width)+1; i < dict->width; i++)
    if (dict->chains[i]) return dict->chains[i];
  return NULL;
}

dict_t*
dict_create ()
{
  dict_t *dict = allocate(sizeof(dict_t));
  dict_init(dict, PRIME_1000);
  dict->compare = dict_str_compare;
  dict->hash = dict_str_hash;
  return dict;
}

void
dict_empty (dict_t *dict)
{
  if (dict->empty) dict->empty(dict);
  for (int i = 0; i < dict->width; i++)
  {
    while (dict->chains[i])
    {
      dict_node_t *node = dict->chains[i];
      dict->chains[i] = node->next;
      free(node);
    }
  }
  dict->count = 0;
}

void
dict_free (dict_t *dict)
{
  dict_empty(dict);
  free(dict->chains);
  free(dict);
}

size_t
dict_count (dict_t *dict)
{
  return dict->count;
}

void
dict_empty_free (dict_t *dict)
{
  dict_each(dict, void *key, void *val) { free(key); free(val); }
}

void
dict_empty_free_keys (dict_t *dict)
{
  dict_each_key(dict, void *key) free(key);
}

void
dict_empty_free_vals (dict_t *dict)
{
  dict_each_val(dict, void *val) free(val);
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
json_create ()
{
  json_t *json = allocate(sizeof(json_t));
  memset(json, 0, sizeof(json_t));
  return json;
}

json_t*
json_parse_boolean (char *subject)
{
  json_t *json = json_create();
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

  json_t *json = json_create();
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

  json_t *json = json_create();
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

  json = json_create();
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

  json = json_create();
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
} pool_t;

void
pool_open (pool_t *pool, char *name, size_t osize, size_t pstep)
{
  osize = max(osize, sizeof(off_t*));

  pool->map  = NULL;
  pool->head = NULL;
  pool->fd   = 0;
  pool->name = strdup(name);

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
  pool->name = NULL;
  pool->head = NULL;
  pool->map = NULL;
}

void*
pool_read (pool_t *pool, off_t position, void *ptr)
{
  ensure(pool->head)
    errorf("atempt to access closed pool");

  ensure(position > 0 && position < pool->head->psize)
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
  ensure(pool->head)
    errorf("atempt to access closed pool");

  ensure(position > 0 && position < pool->head->psize)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  if (ptr && ptr != pool->map + position)
    memmove(pool->map + position, ptr, pool->head->osize);
}

off_t
pool_alloc (pool_t *pool)
{
  ensure(pool->head)
    errorf("atempt to access closed pool");

  if (pool->head->pfree)
  {
    off_t position = pool->head->pfree;
    pool->head->pfree = *((off_t*)(pool->map + pool->head->pfree));
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
  }

  off_t position = pool->head->pnext;
  pool->head->pnext += pool->head->osize;
  return position;
}

void
pool_free (pool_t *pool, off_t position)
{
  ensure(pool->head)
    errorf("atempt to access closed pool");

  ensure(position > 0 && position < pool->head->psize)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  *((off_t*)(pool->map + position)) = pool->head->pfree;
  pool->head->pfree = position;
}

int
pool_is_free (pool_t *pool, off_t position)
{
  for (off_t p = pool->head->pfree; p; p = *((off_t*)(pool->map + p)))
    if (p == position) return 1;
  return 0;
}

off_t
pool_next (pool_t *pool, off_t position)
{
  if (!position)
    position = sizeof(pool_header_t);

  while (position < pool->head->psize && !pool_is_free(pool, position))
    position += pool->head->osize;

  return (position < pool->head->psize) ? position: 0; 
}