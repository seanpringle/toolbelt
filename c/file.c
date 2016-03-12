#define FILE_READ (1<<0)
#define FILE_WRITE (1<<1)
#define FILE_CREATE (1<<2)
#define FILE_RESET (1<<3)
#define FILE_BINARY (1<<4)

typedef struct _file_t {
  FILE *handle;
  char *path;
  uint32_t mode;
} file_t;

file_t*
file_open (char *path, uint32_t mode)
{
  file_t *file = allocate(sizeof(file_t));
  memset(file, 0, sizeof(file_t));

  file->path = str_copy(path, strlen(path));
  file->mode = mode;

  struct stat st;
  int exists = stat(file->path, &st) == 0;

  char fmode[5];
  strcpy(fmode, "r");

  if (mode & (FILE_WRITE|FILE_CREATE))
    strcat(fmode, "+");

  if (mode & FILE_RESET || (!exists && mode & FILE_CREATE))
    strcpy(fmode, "w+");

  if (mode & FILE_BINARY)
    strcat(fmode, "b");

  file->handle = fopen(file->path, fmode);

  if (!file->handle)
  {
    free(file->path);
    free(file);
    return NULL;
  }

  return file;
}

file_t*
file_wrap (FILE *handle, uint32_t mode)
{
  file_t *file = allocate(sizeof(file_t));
  memset(file, 0, sizeof(file_t));

  file->handle = handle;
  file->mode = mode;
  return file;
}

void
file_close (file_t *file)
{
  fclose(file->handle);
  free(file->path);
  free(file);
}

void
file_unwrap (file_t *file)
{
  free(file);
}

off_t
file_tell (file_t *file)
{
  return ftello(file->handle);
}

int
file_seek (file_t *file, off_t position)
{
  return fseeko(file->handle, position, SEEK_SET) == 0;
}

int
file_read (file_t *file, void *ptr, size_t bytes)
{
  size_t read = 0;
  for (int i = 0; i < 3; i++)
  {
    read += fread(ptr + read, 1, bytes - read, file->handle);
    if (read == bytes) return 1;
  }
  return 0;
}

int
file_write (file_t *file, void *ptr, size_t bytes)
{
  size_t written = 0;
  for (int i = 0; i < 3; i++)
  {
    written += fwrite(ptr + written, 1, bytes - written, file->handle);
    if (written == bytes) return 1;
  }
  return 0;
}

int
file_printf (file_t *file, char *pattern, ...)
{
  va_list args;
  va_start(args, pattern);
  int len = vfprintf(file->handle, pattern, args);
  va_end(args);
  return len;
}

char*
file_read_line (file_t *file)
{
  return str_fgets(file->handle);
}

void*
file_slurp (char *path, size_t *size)
{
  void *ptr = NULL;

  struct stat st;
  if (stat(path, &st) != 0)
    return NULL;

  FILE *file = fopen(path, "r");
  if (!file) return NULL;

  size_t bytes = st.st_size;
  ptr = allocate(bytes + 1);

  size_t read = 0;
  for (int i = 0; i < 3; i++)
  {
    read += fread(ptr + read, 1, bytes - read, file);
    if (read == bytes) break;
  }
  if (read != bytes)
    goto fail;

  if (size)
    *size = bytes;
  ((char*)ptr)[bytes] = 0;
  fclose(file);
  return ptr;

fail:
  fclose(file);
  free(ptr);
  return NULL;
}

int
file_blurt (char *path, void *ptr, size_t bytes)
{
  FILE *file = fopen(path, "w");
  if (!file) return 0;

  size_t written = 0;
  for (int i = 0; i < 3; i++)
  {
    written += fwrite(ptr + written, 1, bytes - written, file);
    if (written == bytes) break;
  }

  fclose(file);

  return written == bytes;
}