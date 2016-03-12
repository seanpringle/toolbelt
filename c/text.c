typedef struct _text_t {
  char *buffer;
  size_t bytes;
  off_t cursor;
} text_t;

static char *text_nil = "";

void
text_clear (text_t *text)
{
  if (text->buffer)
    free(text->buffer);

  text->buffer = NULL;
  text->bytes  = 0;
  text->cursor = 0;
}

void
text_free (text_t *text)
{
  text_clear(text);
  free(text);
}

char*
text_unwrap (text_t *text)
{
  char *str = text->buffer;
  free(text);
  return str;
}

char*
text_at (text_t *text, off_t pos)
{
  text->cursor = min(text->bytes - 1, max(0, pos));
  return &text->buffer[text->cursor];
}

char*
text_go (text_t *text, int offset)
{
  text->cursor = min(text->bytes - 1, max(0, (int64_t)text->cursor + offset));
  return &text->buffer[text->cursor];
}

off_t
text_pos (text_t *text)
{
  return text->cursor;
}

void
text_set (text_t *text, char *str)
{
  text_clear(text);

  text->bytes = strlen(str) + 1;
  text->buffer = allocate(text->bytes);
  text->cursor = 0;
  strcpy(text->buffer, str);
}

void
text_ins (text_t *text, char *str)
{
  if (!text->buffer)
  {
    text_set(text, str);
    text->cursor = text->bytes - 1;
    return;
  }

  size_t new_bytes = strlen(str);
  text->buffer = reallocate(text->buffer, text->bytes + new_bytes);
  memmove(&text->buffer[text->cursor + new_bytes], &text->buffer[text->cursor], text->bytes - text->cursor);
  memmove(&text->buffer[text->cursor], str, new_bytes);
  text->cursor += new_bytes;
  text->bytes += new_bytes;
}

void
text_del (text_t *text, size_t bytes)
{
  if (text->buffer)
  {
    bytes = min(text->bytes - text->cursor - 1, bytes);
    memmove(&text->buffer[text->cursor],
      &text->buffer[text->cursor + bytes],
      text->bytes - text->cursor - bytes
    );
    text->bytes -= bytes;
    text_at(text, text->cursor);
  }
}

char*
text_get (text_t *text)
{
  return text->buffer ? text->buffer: text_nil;
}

int
text_cmp (text_t *text, char *str)
{
  return text->buffer ? strcmp(text->buffer, str): -1;
}

size_t
text_count (text_t *text)
{
  return text->buffer ? text->bytes-1: 0;
}

size_t
text_len (text_t *text)
{
  if (text->buffer)
  {
    size_t i = 0, j = 0;
    char *s = text->buffer;
    while (s[i])
    {
      if ((s[i] & 0xC0) != 0x80)
        j++;
      i++;
    }
    return j;
  }
  return 0;
}

size_t
text_scan (text_t *text, str_cb_ischar cb)
{
  if (text->buffer)
  {
    size_t n = str_scan(text->buffer + text->cursor, cb);
    text->cursor += n;
    return n;
  }
  return 0;
}

size_t
text_skip (text_t *text, str_cb_ischar cb)
{
  if (text->buffer)
  {
    size_t n = str_skip(text->buffer + text->cursor, cb);
    text->cursor += n;
    return n;
  }
  return 0;
}

size_t
text_find (text_t *text, char *str)
{
  if (text->buffer)
  {
    char *s = strstr(text->buffer + text->cursor, str);
    if (s)
    {
      text->cursor = s - text->buffer;
      return 1;
    }
  }
  return 0;
}

size_t
text_trim (text_t *text, str_cb_ischar cb)
{
  if (text->buffer)
  {
    str_trim(text->buffer, cb);
    text->bytes = strlen(text->buffer) + 1;
    text->cursor = min(text->bytes-1, text->cursor);
  }
  return max(text->bytes-1, 0);
}

#define TEXT_HEX STR_ENCODE_HEX
#define TEXT_SQL STR_ENCODE_SQL
#define TEXT_DQUOTE STR_ENCODE_DQUOTE

int
text_encode (text_t *text, int type)
{
  if (text->buffer)
  {
    char *result = str_encode(text->buffer, type);
    text_clear(text);
    text_set(text, result);
    return 1;
  }
  return 0;
}

int
text_decode (text_t *text, int type)
{
  if (text->buffer)
  {
    char *err = NULL;
    char *result = str_decode(text->buffer, &err, type);
    if (err == text->buffer + text->bytes - 1)
    {
      text_clear(text);
      text_set(text, result);
      return 1;
    }
  }
  return 0;
}

int
text_match (text_t *text, regex_t *re)
{
  return text->buffer ? regmatch(re, text->buffer + text->cursor): 0;
}

text_t*
text_init (text_t *text)
{
  text->buffer = NULL;
  text->bytes = 0;
  text->cursor = 0;
  return text;
}

text_t*
text_new (char *str)
{
  text_t *text = allocate(sizeof(text_t));
  text_init(text);
  if (str) text_set(text, str);
  return text;
}

text_t*
text_wrap (char *str)
{
  text_t *text = allocate(sizeof(text_t));
  text_init(text);
  text->bytes = strlen(str) + 1;
  text->buffer = str;
  text->cursor = 0;
  return text;
}

text_t*
text_copy (text_t *text)
{
  return text_new(text_get(text));
}

text_t*
text_take (text_t *text, off_t pos, size_t len)
{
  text_t *new = text_new(NULL);
  if (text->buffer)
  {
    pos = pos >= 0 ? min(text->bytes-1, pos): max(0, (int64_t)text->bytes + pos - 1);

    int available = text->bytes - pos - 1;

    len = len >= 0 ? min(len, available): max(0, available + len);

    new->bytes = len + 1;
    new->buffer = allocate(new->bytes);
    memmove(new->buffer, &text->buffer[pos], len);
    new->buffer[len] = 0;
  }
  return new;
}

void text_home (text_t *text) { text_at(text, 0); }
off_t text_end (text_t *text) { text_at(text, text_count(text)); return text->cursor; }

#define textf(...) ({ char *_s = strf(__VA_ARGS__); text_t *_t = text_new(_s); free(_s); text_end(_t); _t; })
#define textf_set(o,...) ({ text_t *_t = (o); char *_s = strf(__VA_ARGS__); text_set(_t, _s); free(_s); _t; })
#define textf_ins(o,...) ({ text_t *_t = (o); char *_s = strf(__VA_ARGS__); text_ins(_t, _s); free(_s); _t; })
