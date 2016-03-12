
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
