
char*
str_fgets (FILE *file)
{
  size_t bytes = 100;
  char *line = allocate(bytes+1);
  line[0] = 0;

  while (!feof(file) && !ferror(file) && fgets(line + bytes - 100, 101, file) && !strchr(line + bytes - 100, '\n'))
  {
    bytes += 100;
    line = reallocate(line, bytes+1);
  }
  if (ferror(file) || (!line[0] && feof(file)))
  {
    free(line);
    line = NULL;
  }
  return line;
}

char*
strf (char *pattern, ...)
{
  char *result = NULL;
  va_list args;
  char buffer[8];

  va_start(args, pattern);
  int len = vsnprintf(buffer, sizeof(buffer), pattern, args);
  va_end(args);

  if (len > -1 && (result = allocate(len+1)) && result)
  {
    va_start(args, pattern);
    vsnprintf(result, len+1, pattern, args);
    va_end(args);
  }
  return result;
}

typedef struct { off_t index; char *subject; int l1; } str_each_t;

#define str_each(l,_val_) for ( \
  str_each_t loop = { 0, (l), 0 }; \
    loop.subject && !loop.l1 && loop.subject[loop.index] && (loop.l1 = 1); \
    loop.index++ \
  ) \
    for (_val_ = loop.subject[loop.index]; loop.l1; loop.l1 = !loop.l1)

typedef int (*str_cb_ischar)(int);

int str_eq  (char *a, char *b) { return a && b && !strcmp(a, b); }
int str_ne  (char *a, char *b) { return a && b &&  strcmp(a, b); }
int str_lt  (char *a, char *b) { return a && b &&  strcmp(a, b) <  0; }
int str_lte (char *a, char *b) { return a && b &&  strcmp(a, b) <= 0; }
int str_gt  (char *a, char *b) { return a && b &&  strcmp(a, b) >  0; }
int str_gte (char *a, char *b) { return a && b &&  strcmp(a, b) >= 0; }

int
str_skip (char *s, str_cb_ischar cb)
{
  int n = 0;
  str_each(s, char c)
  {
    if (!cb(c)) break;
    n++;
  }
  return n;
}

int
str_scan (char *s, str_cb_ischar cb)
{
  int n = 0;
  str_each(s, char c)
  {
    if (cb(c)) break;
    n++;
  }
  return n;
}

char*
str_ltrim (char *str, str_cb_ischar cb)
{
  char *left = str + str_skip(str, cb);
  size_t len = strlen(left);
  memmove(str, left, len+1);
  return str;
}

char*
str_rtrim (char *str, str_cb_ischar cb)
{
  for (
    char *p = s + strlen(str) - 1;
    p >= str && *p && cb(*p);
    *p = 0, p--
  );
  return str;
}

char*
str_rtrim (char *str, str_cb_ischar cb)
{
  str_ltrim(str, cb);
  str_rtrim(str, cb);
  return str;
}

int istab (int c) { return c == '\t'; }
int iscomma (int c) { return c == ','; }
int isperiod (int c) { return c == '.'; }
int isforwardslash (int c) { return c == '/'; }
int isbackslash (int c) { return c == '\\'; }
int isdquote (int c) { return c == '"'; }
int issquote (int c) { return c == '\''; }
int isname (int c) { return isalnum(c) || c == '_'; }
int iscolon (int c) { return c == ':'; }
int issemicolon (int c) { return c == ';'; }
int isquestion (int c) { return c == '?'; }
int isequals (int c) { return c == '='; }
int isampersand (int c) { return c == '&'; }

char*
str_copy (char *s, size_t length)
{
  char *a = allocate(length+1);
  memmove(a, s, length);
  a[length] = 0;
  return a;
}

#define STR_ENCODE_HEX 1
#define STR_ENCODE_SQL 2
#define STR_ENCODE_DQUOTE 3
#define STR_ENCODE_JSON 4

char*
str_encode (char *s, int format)
{
  char *result = NULL;

  static char* hex256[256] = {
    "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0a", "0b", "0c", "0d", "0e", "0f",
    "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "1a", "1b", "1c", "1d", "1e", "1f",
    "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "2a", "2b", "2c", "2d", "2e", "2f",
    "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "3a", "3b", "3c", "3d", "3e", "3f",
    "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "4a", "4b", "4c", "4d", "4e", "4f",
    "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "5a", "5b", "5c", "5d", "5e", "5f",
    "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "6a", "6b", "6c", "6d", "6e", "6f",
    "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "7a", "7b", "7c", "7d", "7e", "7f",
    "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "8a", "8b", "8c", "8d", "8e", "8f",
    "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "9a", "9b", "9c", "9d", "9e", "9f",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "aa", "ab", "ac", "ad", "ae", "af",
    "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8", "b9", "ba", "bb", "bc", "bd", "be", "bf",
    "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8", "c9", "ca", "cb", "cc", "cd", "ce", "cf",
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "da", "db", "dc", "dd", "de", "df",
    "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8", "e9", "ea", "eb", "ec", "ed", "ee", "ef",
    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "fa", "fb", "fc", "fd", "fe", "ff",
  };

  if (format == STR_ENCODE_HEX)
  {
    int length = strlen(s);
    int bytes = length * 2 + 1;
    result = allocate(bytes);
    for (int i = 0; s[i]; i++)
      strcpy(&result[i*2], hex256[(unsigned char)s[i]]);
    result[bytes-1] = 0;
  }
  else
  if (format == STR_ENCODE_SQL)
  {
    int length = strlen(s) + 100;
    int bytes = length * 2 + 1;
    result = allocate(bytes);
    strcpy(result, "convert_from(decode('");
    int offset = strlen(result);
    for (int i = 0; s[i]; i++, offset += 2)
      strcpy(&result[offset], hex256[(unsigned char)s[i]]);
    strcpy(&result[offset], "', 'hex'), 'UTF8')");
  }
  else
  if (format == STR_ENCODE_DQUOTE)
  {
    int length = 0;
    result = allocate(8);

    result[length++] = '"';
    result[length] = 0;

    str_each(s, char c)
    {
      result = reallocate(result, length + 8);

           if (c == 0x07) { strcpy(result+length, "\\a"); length += 2; }
      else if (c == 0x08) { strcpy(result+length, "\\b"); length += 2; }
      else if (c == 0x0c) { strcpy(result+length, "\\f"); length += 2; }
      else if (c == 0x0a) { strcpy(result+length, "\\n"); length += 2; }
      else if (c == 0x0d) { strcpy(result+length, "\\r"); length += 2; }
      else if (c == 0x09) { strcpy(result+length, "\\t"); length += 2; }
      else if (c == 0x0B) { strcpy(result+length, "\\v"); length += 2; }
      else if (c == '\\') { strcpy(result+length, "\\\\"); length += 2; }
      else if (c ==  '"') { strcpy(result+length, "\\\""); length += 2; }
      else                { result[length++] = c; result[length] = 0; }
    }

    result = reallocate(result, length + 8);
    result[length++] = '"';
    result[length] = 0;
  }
  else
  if (format == STR_ENCODE_JSON)
  {
    char *e = NULL;

    if (!s)
      return strf("null");

    int len = strlen(s);

    int64_t d64 = strtoll(s, &e, 0);
    if (s + len == e)
      return strf("%ld", d64);

    double dn = strtod(s, &e);
    if (s + len == e)
      return strf("%10e", dn);

    return str_encode(s, STR_ENCODE_DQUOTE);
  }
  else
  {
    ensure(0)
      errorf("str_encode() unknown format: %d", format);
  }
  return result;
}

char*
str_decode (char *s, char **e, int format)
{
  char *result = NULL;
  if (format == STR_ENCODE_HEX)
  {
    char buf[3];
    int bytes = strlen(s) / 2 + 1;
    result = allocate(bytes);
    for (int i = 0; s[i]; i += 2)
    {
      strncpy(buf, &s[i], 2); buf[2] = 0;
      result[i/2] = strtol(buf, NULL, 16);
    }
    result[bytes-1] = 0;

    if (e)
      *e = s+bytes;
  }
  else
  if (format == STR_ENCODE_DQUOTE)
  {
    if (e)
      *e = s;

    if (*s++ != '"')
      goto done;

    size_t length = 0, limit = 32;
    result = allocate(32);

    str_each(s, char c)
    {
      if (e)
        *e = s + loop.index;

      if (c == '"')
      {
        if (e)
          *e = s + loop.index + 1;
        break;
      }

      if (c == '\\')
      {
        c = s[++loop.index];

             if (c == 'a')  c = '\a';
        else if (c == 'b')  c = '\b';
        else if (c == 'f')  c = '\f';
        else if (c == 'n')  c = '\n';
        else if (c == 'r')  c = '\r';
        else if (c == 't')  c = '\t';
        else if (c == 'v')  c = '\v';
      }

      if (length >= limit-1)
      {
        limit += 32;
        result = reallocate(result, limit+1);
      }

      result[length++] = c;
      result[length] = 0;
    }
  }
done:
  return result;
}
