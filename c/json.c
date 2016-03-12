
#define JSON_OBJECT 1
#define JSON_ARRAY 2
#define JSON_STRING 3
#define JSON_NUMBER 4
#define JSON_INTEGER 5
#define JSON_DOUBLE 6
#define JSON_BOOLEAN 7

typedef struct _json_t {
  int type;
  char *start;
  size_t length;
  struct _json_t *sibling;
  struct _json_t *children;
} json_t;

json_t* json_parse (char *subject);

#define json_is_integer(j) ((j) && (j)->type == JSON_INTEGER)
#define json_is_double(j) ((j) && (j)->type == JSON_DOUBLE)
#define json_is_string(j) ((j) && (j)->type == JSON_STRING)
#define json_is_array(j)  ((j) && (j)->type == JSON_ARRAY)
#define json_is_object(j) ((j) && (j)->type == JSON_OBJECT)
#define json_is_boolean(j) ((j) && (j)->type == JSON_BOOLEAN)

#define json_double(j) strtod((j)->start, NULL)
#define json_integer(j) strtoll((j)->start, NULL, 0)
#define json_boolean(j) (strchr("tT", (j)->start[0]) != NULL)

char*
json_string (json_t *json)
{
  return isalpha(json->start[0]) ? str_copy(json->start, str_skip(json->start, isname)): str_decode(json->start, NULL, STR_ENCODE_DQUOTE);
}

json_t*
json_new ()
{
  json_t *json = allocate(sizeof(json_t));
  memset(json, 0, sizeof(json_t));
  return json;
}

json_t*
json_parse_boolean (char *subject)
{
  json_t *json = json_new();
  json->type   = JSON_BOOLEAN;
  json->start  = subject;
  json->length = str_skip(subject, isalpha);
  return json;
}

json_t*
json_parse_number (char *subject)
{
  char *e1 = NULL;
  char *e2 = NULL;

  strtoll(subject, &e1, 0);
  strtod(subject, &e2);

  json_t *json = json_new();
  json->type   = JSON_NUMBER;
  json->start  = subject;

  if (e1 >= e2)
  {
    json->length = e1 - subject;
    json->type = JSON_INTEGER;
  }
  else
  {
    json->length = e2 - subject;
    json->type = JSON_DOUBLE;
  }
  return json;
}

json_t*
json_parse_string (char *subject)
{
  char *end = NULL;

  if (subject[0] == '"')
  {
    char *str = str_decode(subject, &end, STR_ENCODE_DQUOTE);
    free(str);
  }
  else
  {
    end = subject + str_skip(subject, isname);
  }

  json_t *json = json_new();
  json->type   = JSON_STRING;
  json->start  = subject;
  json->length = end - subject;

  return json;
}

json_t*
json_parse_array (char *subject)
{
  json_t *json = NULL;
  char *start = subject;

  if (*subject++ != '[')
    goto done;

  json = json_new();
  json->type   = JSON_ARRAY;
  json->start  = start;

  json_t *last = NULL;

  while (subject && *subject)
  {
    subject += str_skip(subject, isspace);

    int c = subject[0];

    if (!c)
      break;

    if (c == ']')
    {
      subject++;
      break;
    }

    if (c == ',')
    {
      subject++;
      continue;
    }

    json_t *item = json_parse(subject);

    if (!item || !item->length)
      break;

    subject = item->start + item->length;

    if (last)
    {
      last->sibling = item;
      last = item;
    }
    else
    {
      last = item;
      json->children = item;
    }
  }
  json->length = subject - start;

done:
  return json;
}

json_t*
json_parse_object (char *subject)
{
  json_t *json = NULL;
  char *start = subject;

  if (*subject++ != '{')
    goto done;

  json = json_new();
  json->type   = JSON_OBJECT;
  json->start  = start;

  json_t *last = NULL;

  while (subject && *subject)
  {
    subject += str_skip(subject, isspace);

    int c = *subject;

    if (!c)
      break;

    if (c == '}')
    {
      subject++;
      break;
    }

    if (c == ',' || c == ':')
    {
      subject++;
      continue;
    }

    json_t *item = json_parse(subject);

    if (!item || !item->length)
      break;

    subject = item->start + item->length;

    if (last)
    {
      last->sibling = item;
      last = item;
    }
    else
    {
      last = item;
      json->children = item;
    }
  }
  json->length = subject - start;

done:
  return json;
}

json_t*
json_parse (char *subject)
{
  subject += str_skip(subject, isspace);

  int c = subject[0];
  json_t *child = NULL;

  if (c == '{')
    child = json_parse_object(subject);
  else if (c == '[')
    child = json_parse_array(subject);
  else if (strchr("tTfF", c))
    child = json_parse_boolean(subject);
  else if (c == '"' || isalpha(c))
    child = json_parse_string(subject);
  else
    child = json_parse_number(subject);
  return child;
}

void
json_free (json_t *json)
{
  while (json && json->children)
  {
    json_t *item = json->children;
    json->children = item->sibling;
    json_free(item);
  }
  free(json);
}

json_t*
json_object_get (json_t *json, char *name)
{
  if (!json || json->type != JSON_OBJECT)
    return NULL;

  for (json_t *key = json->children; key && key->sibling; key = key->sibling)
  {
    if (key->type == JSON_STRING && key->sibling)
    {
      char *str = json_string(key);
      int found = !strcmp(str, name);
      free(str);
      if (found) return key->sibling;
    }
    key = key->sibling;
  }
  return NULL;
}

json_t*
json_array_get (json_t *json, int index)
{
  if (!json || json->type != JSON_ARRAY)
    return NULL;

  int i = 0;
  for (json_t *item = json->children; item; item = item->sibling)
  {
    if (i == index) return item;
    i++;
  }
  return NULL;
}
