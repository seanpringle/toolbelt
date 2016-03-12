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
  unsigned char *bitmap;
} pool_t;

void
pool_bitmap (pool_t *pool)
{
  free(pool->bitmap);

  pool->bitmap = allocate(pool->head->psize / 8 + 1);
  memset(pool->bitmap, 0, pool->head->psize / 8 + 1);

  for (off_t pos = pool->head->pfree; pos; pos = *((off_t*)(pool->map + pos)))
    pool->bitmap[pos / 8] |= 1 << (pos % 8);
}

void
pool_open (pool_t *pool, char *name, size_t osize, size_t pstep)
{
  osize = max(osize, sizeof(off_t*));

  pool->map  = NULL;
  pool->head = NULL;
  pool->fd   = 0;
  pool->name = strdup(name);
  pool->bitmap = NULL;

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

  pool_bitmap(pool);
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
  free(pool->bitmap);
  pool->name = NULL;
  pool->head = NULL;
  pool->map = NULL;
}

void*
pool_read (pool_t *pool, off_t position, void *ptr)
{
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize)
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
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  if (ptr && ptr != pool->map + position)
    memmove(pool->map + position, ptr, pool->head->osize);
}

off_t
pool_alloc (pool_t *pool)
{
  if (pool->head->pfree)
  {
    off_t position = pool->head->pfree;
    pool->head->pfree = *((off_t*)(pool->map + pool->head->pfree));
    pool->bitmap[position / 8] &= ~(1 << (position % 8));
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
    pool_bitmap(pool);
  }

  off_t position = pool->head->pnext;
  memset(pool->map + position, 0, pool->head->osize);
  pool->head->pnext += pool->head->osize;
  return position;
}

void
pool_free (pool_t *pool, off_t position)
{
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  *((off_t*)(pool->map + position)) = pool->head->pfree;
  pool->head->pfree = position;

  pool->bitmap[position / 8] |= 1 << (position % 8);
}

void
pool_sync (pool_t *pool)
{
  ensure(msync(pool->map, pool->head->pnext, MS_SYNC) == 0)
    errorf("pool sync failed: %s", pool->name);
}

int
pool_is_free (pool_t *pool, off_t position)
{
  return position >= pool->head->pnext || pool->bitmap[position / 8] & 1 << (position % 8) ? 1:0;
}

off_t
pool_next (pool_t *pool, off_t position)
{
  position += position ? pool->head->osize: sizeof(pool_header_t);

  while (position < pool->head->psize && pool_is_free(pool, position))
    position += pool->head->osize;

  return (position < pool->head->psize) ? position: 0;
}

typedef struct { off_t index; pool_t *pool; off_t pos; int l1; } pool_each_t;

#define pool_each(l,_val_) for ( \
  pool_each_t loop = { 0, (l), 0, 0 }; \
    !loop.l1 && (loop.pos = pool_next(loop.pool, loop.pos)) && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = pool_read(loop.pool, loop.pos, NULL); loop.l1; loop.l1 = !loop.l1)

void*
pool_read_chunk (pool_t *pool, off_t position, size_t bytes, void *ptr)
{
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize - bytes)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  if (ptr)
  {
    memmove(ptr, pool->map + position, bytes);
    return ptr;
  }

  return pool->map + position;
}

void
pool_write_chunk (pool_t *pool, off_t position, size_t bytes, void *ptr)
{
  ensure(position >= sizeof(pool_header_t) && position < pool->head->psize - bytes)
    errorf("attempt to access outside pool: %lu %s", position, pool->name);

  if (ptr && ptr != pool->map + position)
    memmove(pool->map + position, ptr, bytes);
}

off_t
pool_alloc_chunk (pool_t *pool, size_t bytes)
{
  if (bytes <= pool->head->osize)
    return pool_alloc(pool);

  size_t slots = (bytes / pool->head->osize) + (bytes % pool->head->osize ? 1:0);

  for (off_t pos = pool->head->pfree; pos; pos = *((off_t*)(pool->map + pos)))
  {
    int found = 1;
    for (int i = 0; found && i < slots; i++)
      found = pool_is_free(pool, pos + (i * pool->head->osize));

    if (found)
    {
      for (int i = 0; i < slots; i++)
        pool->bitmap[(pos + (i * pool->head->osize)) / 8] &= ~(1 << ((pos + (i * pool->head->osize)) % 8));
      return pos;
    }
  }

  off_t pfree = pool->head->pfree;
  pool->head->pfree = 0;

  off_t pos = pool->head->pnext;

  for (int i = 0; i < slots; i++)
    pool_alloc(pool);

  pool->head->pfree = pfree;
  return pos;
}

void
pool_free_chunk (pool_t *pool, off_t pos, size_t bytes)
{
  size_t slots = bytes / pool->head->osize + (bytes % pool->head->osize ? 1:0);

  for (int i = 0; i < slots; i++)
    pool_free(pool, pos + (i * pool->head->osize));
}