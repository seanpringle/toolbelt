
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
array_clear_free (array_t *array)
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
