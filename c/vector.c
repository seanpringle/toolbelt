
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
vector_clear_free (vector_t *vector)
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
