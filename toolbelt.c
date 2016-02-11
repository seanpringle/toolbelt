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

#define PRIME_1000 997
#define PRIME_10000 9973
#define PRIME_100000 99991
#define PRIME_1000000 999983

#define ensure(x) for ( ; !(x) ; exit(EXIT_FAILURE) )
#define errorf(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while(0)

typedef int (*callback)(void*);

void*
allocate (size_t bytes)
{
  return malloc(bytes);
}

void
release (void *ptr, size_t bytes)
{
  free(ptr);
}

int
regmatch (regex_t *re, const char *subject)
{
  return regexec(re, subject, 0, NULL, 0) == 0;
}

uint32_t
djb_hash (const char *str)
{
  uint32_t hash = 5381;
  for (int i = 0; str[i]; hash = hash * 33 + str[i++]);
  return hash;
}

char*
mprintf (const char *pattern, ...)
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

typedef int (*str_cb_ischar)(int);

char*
str_skip (const char *s, str_cb_ischar cb)
{
  while (s && *s && cb(*s)) s++;
  return (char*)s;
}

char*
str_scan (const char *s, str_cb_ischar cb)
{
  while (s && *s && !cb(*s)) s++;
  return (char*)s;
}

int
istab (int c)
{
  return c == '\t';
}

int
str_count (char *str, str_cb_ischar cb)
{
  int count = 0;
  while (str && *str)
    if (cb(*str++)) count++;
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

char*
str_encode (const char *s, int format)
{
  char *result = NULL;
  int length = strlen(s);

  if (format == STR_ENCODE_HEX)
  {
    int bytes = length * 2 + 1;
    result = allocate(bytes);
    for (int i = 0; s[i]; i++)
      sprintf(&result[i*2], "%02x", s[i]);
    result[bytes-1] = 0;
  }
  else
  if (format == STR_ENCODE_SQL)
  {
    char *hex = str_encode(s, STR_ENCODE_HEX);
    result = mprintf("convert_from(decode('%s', 'hex'), 'UTF-8')", hex);
    free(hex);
  }
  else
  {
    ensure(0)
      errorf("str_encode() unknown format: %d", format);
  }
  return result;
}

char*
str_decode (const char *s, int format)
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
  }
  return result;
}

typedef struct _list_node_t {
  void *val;
  struct _list_node_t *next;
} list_node_t;

typedef struct _list_t {
  list_node_t *nodes;
} list_t;

void
list_insert (list_t *list, off_t position, void *val)
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
}

void*
list_remove (list_t *list, off_t position)
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
  return *prev;
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
}

list_t*
list_create ()
{
  list_t *list = allocate(sizeof(list_t));
  list_init(list);
  return list;
}

void
list_free (list_t *list)
{
  while (list->nodes)
  {
    list_node_t *node = list->nodes;
    list->nodes = node->next;
    release(node, sizeof(list_node_t));
  }
  release(list, sizeof(list_t));
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
  struct _dict_node_t *next;
} dict_node_t;

typedef struct _dict_t {
  dict_node_t *chains[PRIME_1000];
  dict_cb_cmp compare;
  dict_cb_hash hash;
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
}

int
dict_set (dict_t *dict, void *key, void *val)
{
  int chain = dict->hash(key) % PRIME_1000;

  dict_node_t *node = dict->chains[chain];
  while (node && !dict->compare(node->key, key))
    node = node->next;

  if (!node)
  {
    node = allocate(sizeof(dict_node_t));
    node->key = key;
    node->val = val;
    node->next = dict->chains[chain];
    dict->chains[chain] = node;
    return 2;
  }
  node->val = val;
  return 1;
}

dict_node_t*
dict_find (dict_t *dict, void *key)
{
  int chain = dict->hash(key) % PRIME_1000;

  dict_node_t *node = dict->chains[chain];
  while (node && !dict->compare(node->key, key))
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
  while (*prev && !dict->compare((*prev)->key, key))
    prev = &((*prev)->next);

  if (*prev)
  {
    dict_node_t *node = *prev;
    *prev = node->next;
    void *val = node->val;
    release(node, sizeof(dict_node_t));
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

  uint32_t hv = dict->hash(node->key);

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
dict_free (dict_t *dict)
{
  for (int i = 0; i < PRIME_1000; i++)
  {
    while (dict->chains[i])
    {
      dict_node_t *node = dict->chains[i];
      dict->chains[i] = node->next;
      release(node, sizeof(dict_node_t));
    }
  }
  free(dict);
}

#define dict_each(l) for ( \
  struct { int index; dict_t *dict; dict_node_t *node; void *key; void *value; } \
    loop = { 0, (l), dict_first((l)), dict_first((l)) ? dict_first((l))->key: NULL, dict_first((l)) ? dict_first((l))->val: NULL }; \
    loop.node; \
    loop.index++, loop.node = dict_next(loop.dict, loop.node), loop.key = loop.node ? loop.node->key: NULL, loop.value = loop.node ? loop.node->val: NULL \
  )
