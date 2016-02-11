#include "toolbelt.c"

int
main (int argc, char *argv[])
{
  char *item;

  char *a = str_encode("hello world", STR_ENCODE_HEX);
  char *b = str_decode("68656c6c6f20776f726c64", STR_ENCODE_HEX);

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

  ensure(list_count(list) == 2) errorf("list_length");

  ensure((item = list_get(list, 0)) && !strcmp(item, "hello"))
    errorf("list_get");

  ensure((item = list_del(list, 1)) && !strcmp(item, "world"))
    errorf("list_del");

  list_free(list);

  dict_t *dict = dict_create(NULL, NULL);
  ensure(dict) errorf("dict_create");

  dict_set(dict, "hello", "world");
  dict_set(dict, "alpha", "beta");
  dict_set(dict, "fu", "bar");

  ensure((item = dict_get(dict, "hello")) && !strcmp(item, "world"))
    errorf("dict_get");

  dict_free(dict);

  return EXIT_SUCCESS;
}