
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
    map->chains[i].clear = vector_clear_free;
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