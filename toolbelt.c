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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <libgen.h>
#include <math.h>

#define PRIME_1000 997
#define PRIME_10000 9973
#define PRIME_100000 99991
#define PRIME_1000000 999983

#define ensure(x) for ( ; !(x) ; exit(EXIT_FAILURE) )
#define errorf(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while(0)
#define unless(c) if (!(c))

#define min(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a: _b; })
#define max(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a: _b; })

typedef int (*callback)(void*);
typedef void (*vcallback)(void*);

void*
allocate (size_t bytes)
{
  void *ptr = malloc(bytes);
  ensure(ptr) errorf("malloc failed %lu bytes", bytes);
  return ptr;
}

void
release (void *ptr, size_t bytes)
{
  free(ptr);
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
  char *line = allocate(bytes);
  line[0] = 0;

  while (fgets(line + bytes - 100, 100, file) && !strchr(line + bytes - 100, '\n'))
  {
    bytes += 100;
    line = realloc(line, bytes);
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

#define str_each(s) for (struct { int index; char *cursor; char value; } loop = { 0, (s), 0 }; \
  (loop.value = loop.cursor[0]); loop.cursor++)

typedef int (*str_cb_ischar)(int);

int
str_skip (char *s, str_cb_ischar cb)
{
  int count = 0;
  str_each(s)
  {
    if (!cb(loop.value))
      break;
    count++;
  }
  return count;
}

int
str_scan (char *s, str_cb_ischar cb)
{
  int count = 0;
  str_each(s)
  {
    if (cb(loop.value))
      break;
    count++;
  }
  return count;
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

int
istab (int c)
{
  return c == '\t';
}

int
iscomma (int c)
{
  return c == ',';
}

int
isperiod (int c)
{
  return c == '.';
}

int
isforwardslash (int c)
{
  return c == '/';
}

int
isbackslash (int c)
{
  return c == '\\';
}

int
isdquote (int c)
{
  return c == '"';
}

int
issquote (int c)
{
  return c == '\'';
}

int
str_count (char *str, str_cb_ischar cb)
{
  int count = 0;
  str_each(str)
    if (cb(loop.value)) count++;
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
  int length = strlen(s);

  if (format == STR_ENCODE_HEX)
  {
    int bytes = length * 2 + 1;
    result = allocate(bytes);
    for (int i = 0; s[i]; i++)
      sprintf(&result[i*2], "%02x", (unsigned char)s[i]);
    result[bytes-1] = 0;
  }
  else
  if (format == STR_ENCODE_SQL)
  {
    char *hex = str_encode(s, STR_ENCODE_HEX);
    result = mprintf("convert_from(decode('%s', 'hex'), 'UTF8')", hex);
    free(hex);
  }
  else
  if (format == STR_ENCODE_DQUOTE)
  {
    result = mprintf("\"");
    char *change = NULL;

    str_each(s)
    {
      int c = loop.value;

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

    result = mprintf("");
    char *change = NULL;

    str_each(s)
    {
      int c = loop.value;

      if (e)
        *e = loop.cursor;

      if (c == '"')
      {
        if (e)
          *e = loop.cursor+1;
        break;
      }

      if (c == '\\')
      {
        loop.cursor++;
        c = loop.cursor[0];

             if (c == 'a')  c = '\a';
        else if (c == 'b')  c = '\b';
        else if (c == 'f')  c = '\f';
        else if (c == 'n')  c = '\n';
        else if (c == 'r')  c = '\r';
        else if (c == 't')  c = '\t';
        else if (c == 'v')  c = '\v';
      }

      change = mprintf("%s%c", result, c);
      free(result);
      result = change;
    }
  }
done:
  return result;
}

typedef struct _list_node_t {
  void *val;
  struct _list_node_t *next;
} list_node_t;

typedef struct _list_t {
  list_node_t *nodes;
  size_t count;
} list_t;

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
    release(node, sizeof(list_node_t));
    list->count--;
  }
  return val;
}

void*
list_get (list_t *list, off_t position)
{
  list_node_t **prev = &list->nodes;
  for (
    off_t i = 0;
    *prev && i < position;
    prev = &((*prev)->next), i++
  );
  return *prev ? (*prev)->val: NULL;
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

list_node_t*
list_first (list_t *list)
{
  return list_next(list, NULL);
}

void
list_init (list_t *list)
{
  list->nodes = NULL;
  list->count = 0;
}

list_t*
list_create ()
{
  list_t *list = allocate(sizeof(list_t));
  list_init(list);
  return list;
}

void
list_empty (list_t *list, vcallback cb)
{
  while (list->nodes)
  {
    list_node_t *node = list->nodes;
    list->nodes = node->next;
    if (cb) cb(node->val);
    release(node, sizeof(list_node_t));
  }
  list->count = 0;
}

void
list_free (list_t *list)
{
  list_empty(list, NULL);
  release(list, sizeof(list_t));
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

void
list_shove (list_t *list, void *val)
{
  list_ins(list, 0, val);
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

#define list_each(l) for ( \
  struct { int index; list_t *list; list_node_t *node; void *value; } \
    loop = { 0, (l), (l)->nodes, (l)->nodes ? (l)->nodes->val: NULL }; \
    loop.node; \
    loop.index++, loop.node = list_next(loop.list, loop.node), loop.value = loop.node ? loop.node->val: NULL \
  )

typedef int (*dict_cb_cmp)(void*, void*);
typedef uint32_t (*dict_cb_hash)(void*);

typedef struct _dict_node_t {
  void *key;
  void *val;
  uint32_t hash;
  struct _dict_node_t *next;
} dict_node_t;

typedef struct _dict_t {
  dict_node_t *chains[PRIME_1000];
  dict_cb_cmp compare;
  dict_cb_hash hash;
  size_t count;
} dict_t;

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
dict_init (dict_t *dict, dict_cb_hash hash, dict_cb_cmp compare)
{
  memset(dict, 0, sizeof(dict_t));
  dict->hash = hash ? hash: dict_str_hash;
  dict->compare = compare ? compare: dict_str_compare;
  dict->count = 0;
}

int
dict_set (dict_t *dict, void *key, void *val)
{
  int rc = 1;
  uint32_t hv = dict->hash(key);
  int chain = hv % PRIME_1000;

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
  return rc;
}

dict_node_t*
dict_find (dict_t *dict, void *key)
{
  int chain = dict->hash(key) % PRIME_1000;

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
  int chain = dict->hash(key) % PRIME_1000;

  dict_node_t **prev = &dict->chains[chain];
  while (*prev && dict->compare((*prev)->key, key))
    prev = &((*prev)->next);

  if (*prev)
  {
    dict_node_t *node = *prev;
    *prev = node->next;
    void *val = node->val;
    release(node, sizeof(dict_node_t));
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
    for (int i = 0; i < PRIME_1000; i++)
      if (dict->chains[i]) return dict->chains[i];
    return NULL;
  }

  if (node->next)
    return node->next;

  uint32_t hv = node->hash;

  for (int i = (hv % PRIME_1000)+1; i < PRIME_1000; i++)
    if (dict->chains[i]) return dict->chains[i];
  return NULL;
}

dict_node_t*
dict_first (dict_t *dict)
{
  return dict_next(dict, NULL);
}

dict_t*
dict_create (dict_cb_hash hash, dict_cb_cmp compare)
{
  dict_t *dict = allocate(sizeof(dict_t));
  dict_init(dict, hash, compare);
  return dict;
}

void
dict_empty (dict_t *dict, vcallback cbk, vcallback cbv)
{
  for (int i = 0; i < PRIME_1000; i++)
  {
    while (dict->chains[i])
    {
      dict_node_t *node = dict->chains[i];
      dict->chains[i] = node->next;
      if (cbk) cbk(node->key);
      if (cbv) cbv(node->val);
      release(node, sizeof(dict_node_t));
    }
  }
  dict->count = 0;
}

void
dict_free (dict_t *dict)
{
  dict_empty(dict, NULL, NULL);
  free(dict);
}

size_t
dict_count (dict_t *dict)
{
  return dict->count;
}

#define dict_each(l) for ( \
  struct { int index; dict_t *dict; dict_node_t *node; void *key; void *value; } \
    loop = { 0, (l), dict_first((l)), dict_first((l)) ? dict_first((l))->key: NULL, dict_first((l)) ? dict_first((l))->val: NULL }; \
    loop.node; \
    loop.index++, loop.node = dict_next(loop.dict, loop.node), loop.key = loop.node ? loop.node->key: NULL, loop.value = loop.node ? loop.node->val: NULL \
  )

#define JSON_OBJECT 1
#define JSON_ARRAY 2
#define JSON_STRING 3
#define JSON_NUMBER 4

typedef struct _json_t {
  int type;
  char *start;
  size_t length;
  struct _json_t *sibling;
  struct _json_t *children;
} json_t;

json_t* json_parse (char *subject);

json_t*
json_parse_number (char *subject)
{

  char *end = NULL;
  strtod(subject, &end);

  json_t *json = allocate(sizeof(json_t));
  json->type   = JSON_NUMBER;
  json->start  = subject;
  json->length = end - subject;
  json->sibling = NULL;
  json->children = NULL;

  return json;
}

json_t*
json_parse_string (char *subject)
{
  char *end = NULL;

  if (subject[0] != '"')
    return NULL;

  char *str = str_decode(subject, &end, STR_ENCODE_DQUOTE);
  free(str);

  json_t *json = allocate(sizeof(json_t));
  json->type   = JSON_STRING;
  json->start  = subject;
  json->length = end - subject;
  json->sibling = NULL;
  json->children = NULL;

  return json;
}

json_t*
json_parse_array (char *subject)
{

  json_t *json = NULL;
  char *start = subject;

  if (*subject++ != '[')
    goto done;

  json = allocate(sizeof(json_t));
  json->type   = JSON_ARRAY;
  json->start  = start;
  json->length = 0;
  json->sibling = NULL;
  json->children = NULL;

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

  json = allocate(sizeof(json_t));
  json->type   = JSON_OBJECT;
  json->start  = start;
  json->length = 0;
  json->sibling = NULL;
  json->children = NULL;

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
  {
    child = json_parse_object(subject);
  }
  else if (c == '[')
  {
    child = json_parse_array(subject);
  }
  else if (c == '"')
  {
    child = json_parse_string(subject);
  }
  else
  {
    child = json_parse_number(subject);
  }
  return child;
}

void
json_free (json_t *json)
{
  if (json)
  {
    while (json->children)
    {
      json_t *item = json->children;
      json->children = item->sibling;
      json_free(item);
    }
  }
  free(json);
}

dict_t*
json_dict (json_t *json)
{
  if (json->type != JSON_OBJECT)
    return NULL;

  dict_t *dict = dict_create(NULL, NULL);

  for (json_t *key = json->children; key; key = key->sibling)
  {
    if (key->type == JSON_STRING && key->sibling)
      dict_set(dict, str_decode(key->start, NULL, STR_ENCODE_DQUOTE), key->sibling);

    key = key->sibling;
  }
  return dict;
}

void
json_dict_free (dict_t *dict)
{
  dict_empty(dict, free, NULL);
  dict_free(dict);
}

list_t*
json_list (json_t *json)
{
  if (json->type != JSON_ARRAY)
    return NULL;

  list_t *list = list_create();

  for (json_t *item = json->children; item; item = item->sibling)
    list_push(list, item);

  return list;
}

void
json_list_free (list_t *list)
{
  list_free(list);
}
