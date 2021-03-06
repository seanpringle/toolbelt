//#define TOOLBELT_DB
//#define TOOLBELT_THREAD
#include "toolbelt.c"

int
map_cmp_int (void *a, void *b)
{
  int ai = *((int*)a);
  int bi = *((int*)b);
  if (ai < bi) return -1;
  if (ai > bi) return -1;
  return 0;
}

uint32_t
map_hash_int (void *a)
{
  int ai = *((int*)a);
  return ai;
}

int
main (int argc, char *argv[])
{
  char *item;

  char *a = str_encode("hello world", STR_ENCODE_HEX);
  char *b = str_decode("68656c6c6f20776f726c64", NULL, STR_ENCODE_HEX);

  ensure(a && !strcmp(a, "68656c6c6f20776f726c64"))
    errorf("str_encode");

  ensure(b && !strcmp(b, "hello world"))
    errorf("str_decode");

  free(a);
  free(b);

  list_t *list = list_new();
  ensure(list) errorf("list_new");

  list_ins(list, 0, "world");
  list_ins(list, 0, "hello");

  int ln = 0;
  list_each(list, char *str)
    ln += strlen(str);
  ensure(ln == 10)
    errorf("list_each");

  ensure(list_count(list) == 2) errorf("list_length");

  ensure((item = list_get(list, 0)) && !strcmp(item, "hello"))
    errorf("list_get");

  ensure((item = list_get(list, 1)) && !strcmp(item, "world"))
    errorf("list_get");

  ensure((item = list_del(list, 1)) && !strcmp(item, "world"))
    errorf("list_del");

  ensure(list_set(list, 0, "goodbye") == 1)
    errorf("list_set 1");

  list_free(list);

  map_t *map = map_new();
  ensure(map) errorf("map_new");

  map_set(map, "hello", "world");
  map_set(map, "alpha", "beta");
  map_set(map, "fu", "bar");

  int dn = 0;
  map_each(map, char *key, char *val)
  {
    dn += strlen(key);
    dn += strlen(val);
  }
  ensure(dn == 24)
    errorf("map_each");

  ensure((item = map_get(map, "hello")) && !strcmp(item, "world"))
    errorf("map_get 1");

  map_free(map);

  map = map_new();
  map->hash = map_hash_int;
  map->compare = map_cmp_int;

  int i1 = 1;
  int i2 = 2;
  int i3 = 3;
  int i4 = 4;

  map_set(map, &i1, &i2);
  map_set(map, &i3, &i4);

  ensure(map_get(map, &i1) == &i2)
    errorf("map_get 2");

  ensure(map_get(map, &i4) == NULL)
    errorf("map_get 3");

  map_free(map);

  map = map_new();
  map->clear = map_clear_free;

  for (int i = 0; i < 10000; i++)
  {
    char tmp[32];
    sprintf(tmp, "%d", i);
    map_set(map, strdup(tmp), strdup(tmp));
  }

  map_free(map);

  ensure(str_skip("hello", isspace) == 0)
    errorf("str_skip");

  ensure(str_scan("hello", isspace) == 5)
    errorf("str_skip");

  char *dquote = str_decode("\"hello\\nworld\\n\"", NULL, STR_ENCODE_DQUOTE);
  ensure(!strcmp(dquote, "hello\nworld\n"))
    errorf("str_decode DQUOTE");
  free(dquote);

  dquote = str_encode("\"hello\nworld\"", STR_ENCODE_DQUOTE);
  ensure(!strcmp(dquote, "\"\\\"hello\\nworld\\\"\""))
    errorf("str_encode DQUOTE");
  free(dquote);

  json_t *json, *jval;

  json = json_parse("{\"alpha\": 1, \"beta\": 2, \"gamma\": [1, 2, 3] }");

  ensure(json)
    errorf("json_parse 1");

  json_free(json);

  json = json_parse("{alpha: 1, beta: 2}");

  ensure(json && (jval = json_object_get(json, "alpha")) && json_is_integer(jval) && json_integer(jval) == 1)
    errorf("json_parse 2");

  json_free(json);

  pool_t pool;
  unlink("pool");
  pool_open(&pool, "pool", sizeof(uint32_t), 1000);

  uint32_t pc1 = 0;

  for (uint32_t i = 0; i < 2000; i++)
  {
    off_t pos = pool_alloc(&pool);
    pool_write(&pool, pos, &i);
    pc1 += i;
  }

  uint32_t pc2 = 0;

  pool_each(&pool, uint32_t *i)
    pc2 += *i;

  ensure(pc1 == pc2)
    errorf("pool_next %u %u", pc1, pc2);

  pool_close(&pool);

  unlink("pool");
  pool_open(&pool, "pool", 4, 1000);

  strcpy(pool_read_chunk(&pool, pool_alloc_chunk(&pool, 6), 6, NULL), "hello");
  strcpy(pool_read_chunk(&pool, pool_alloc_chunk(&pool, 6), 6, NULL), "world");
  strcpy(pool_read_chunk(&pool, pool_alloc_chunk(&pool, 17), 17, NULL), "once upon a time");
  strcpy(pool_read_chunk(&pool, pool_alloc_chunk(&pool, 2), 2, NULL), "A");

  pool_close(&pool);

  vector_t *v = vector_new();
  vector_push(v, "hello");
  vector_push(v, "world");
  vector_del(v, 0);
  vector_push(v, "hello");

  vector_each(v, char *s)
    printf("%lu => %s\n", loop.index, s);

  vector_free(v);

  array_t *ar = array_new(10);
  array_set(ar, 0, "hello");
  array_set(ar, 1, "world");

  array_each(ar, char *s)
    printf("%lu => %s\n", loop.index, s ? s: "(clear)");

  array_free(ar);

  text_t *text = textf("hello");
  text_at(text, text_count(text));
  text_ins(text, " world");
  text_at(text, 0);
  text_del(text, 1);
  text_ins(text, "I say, ");
  text_t *text2 = text_take(text, 0, 1);
  text_t *text3 = text_take(text, 5, 5);
  text_t *text4 = text_take(text, -5, 5);

  printf("%s\n", text_get(text));
  printf("%s\n", text_get(text2));
  printf("%s\n", text_get(text3));
  printf("%s\n", text_get(text4));

  text_free(text);
  text_free(text2);
  text_free(text3);
  text_free(text4);

  file_t *file = file_open("fubar", FILE_CREATE);
  file_write(file, "fu\n", 3);
  file_write(file, "bar\n", 4);
  file_close(file);

  char *fubar = file_slurp("fubar", NULL);

  ensure(fubar && !strcmp(fubar, "fu\nbar\n"))
    errorf("file_slurp");

  free(fubar);

  unlink("fubar");
  unlink("pool");

  return EXIT_SUCCESS;
}
