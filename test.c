#include "toolbelt.c"

int
dict_cmp_int (void *a, void *b)
{
  int ai = *((int*)a);
  int bi = *((int*)b);
  if (ai < bi) return -1;
  if (ai > bi) return -1;
  return 0;
}

uint32_t
dict_hash_int (void *a)
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

  char *tsv = "alpha\tbeta\tgamma";

  ensure(str_is_tsv(tsv, 3))
    errorf("str_is_tsv");

  list_t *list = list_create();
  ensure(list) errorf("list_create");

  list_ins(list, 0, "world");
  list_ins(list, 0, "hello");

  list_each(list, char*)
    printf("%s\n", loop.value);

  ensure(list_count(list) == 2) errorf("list_length");

  ensure((item = list_get(list, 0)) && !strcmp(item, "hello"))
    errorf("list_get");

  ensure((item = list_del(list, 1)) && !strcmp(item, "world"))
    errorf("list_del");

  ensure(list_set(list, 0, "goodbye") == 1)
    errorf("list_set 1");

  list_free(list);

  dict_t *dict = dict_create();
  ensure(dict) errorf("dict_create");

  dict_set(dict, "hello", "world");
  dict_set(dict, "alpha", "beta");
  dict_set(dict, "fu", "bar");

  dict_each(dict, char*, char*)
    printf("%s => %s\n", loop.key, loop.value);

  ensure((item = dict_get(dict, "hello")) && !strcmp(item, "world"))
    errorf("dict_get 1");

  dict_free(dict);

  dict = dict_create(dict_hash_int, dict_cmp_int);

  int i1 = 1;
  int i2 = 2;
  int i3 = 3;
  int i4 = 4;

  dict_set(dict, &i1, &i2);
  dict_set(dict, &i3, &i4);

  ensure(dict_get(dict, &i1) == &i2)
    errorf("dict_get 2");

  ensure(dict_get(dict, &i4) == NULL)
    errorf("dict_get 3");

  dict_free(dict);

  ensure(str_skip("hello", isspace) == 0)
    errorf("str_skip");

  ensure(str_scan("hello", isspace) == 5)
    errorf("str_skip");

  ensure(str_count("hello", isspace) == 0)
    errorf("str_count");

  char *dquote = str_decode("\"hello\\nworld\\n\"", NULL, STR_ENCODE_DQUOTE);
  ensure(!strcmp(dquote, "hello\nworld\n"))
    errorf("str_decode DQUOTE");
  free(dquote);

  json_t *json = json_parse("{\"alpha\": 1, \"beta\": 2, \"gamma\": [1, 2, 3] }");

  ensure(json)
    errorf("json_parse");

  json_free(json);

  return EXIT_SUCCESS;
}
