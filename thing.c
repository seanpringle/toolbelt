
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>

#define ensure(x) for ( ; !(x) ; exit(EXIT_FAILURE) )
#define errorf(...) do { fprintf(stderr, "%s %d ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)

#define ensure_malloc(b) ensure((b)) errorf("malloc failed");
#define ensure_realloc(b) ensure((b)) errorf("realloc failed");

#define min(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a: _b; })
#define max(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a: _b; })

#define PRIME_1000 997
#define PRIME_10000 9973
#define PRIME_100000 99991
#define PRIME_1000000 999983

static int str_eq(char *a, char *b) { return !strcmp(a, b); }

static char*
strf (char *pattern, ...)
{
  char *result = NULL;
  va_list args;
  char buffer[8];

  va_start(args, pattern);
  int len = vsnprintf(buffer, sizeof(buffer), pattern, args);
  va_end(args);

  if (len > -1)
  {
    ensure_malloc((result = malloc(len+1)));
    va_start(args, pattern);
    vsnprintf(result, len+1, pattern, args);
    va_end(args);
  }
  return result;
}

static uint32_t
djb_hash (char *str)
{
  uint32_t hash = 5381;
  for (int i = 0; str[i]; hash = hash * 33 + str[i++]);
  return hash;
}

typedef int64_t word;

#define OK 0
#define WTF 1
#define PAUSE 2

#define QUEUE 10
#define STRING 50
#define MESSAGE 50
#define STACK 25
#define CELLS 1000

#define FLAG_EQ (1<<0)
#define FLAG_LT (1<<1)
#define FLAG_GT (1<<2)

typedef struct _Label {
  int value;
  char *name;
  struct _Label *next;
} Label;

typedef struct _Node {
  word id;
  int state;
  int qp;
  int sp;
  int stack[STACK];
  int flags;
  int skip;
  int ticks;
  char *name;
  char *code;
  char *script;
  char *mark;
  Label *chains[PRIME_1000];
  char message[MESSAGE];
  struct _Node *next;
  word current;
  word previous;
  word *compose;
  word *receive;
  word *queue[QUEUE];
  word array[CELLS];
} Node;

Node *nodes;

#define messagef(t,...) snprintf((t)->message, MESSAGE, __VA_ARGS__)

static Node*
node_new (word id, char *name, char *code)
{
  Node *this;

  ensure_malloc((this = malloc(sizeof(Node))));
  memset(this, 0, sizeof(Node));

  this->state = OK;
  this->id = id;
  this->name = strf("%s", name);
  this->code = strf("%s", code);
  this->script = this->code;

  this->next = nodes;
  nodes = this;

  return this;
}

static void
node_free (Node *this)
{
  for (int i = 0; i < PRIME_1000; i++)
  {
    while (this->chains[i])
    {
      Label *next = this->chains[i]->next;
      free(this->chains[i]->name);
      free(this->chains[i]);
      this->chains[i] = next;
    }
  }
  free(this->receive);
  free(this->name);
  free(this->code);
  free(this);
}

static Node*
node_id (word id)
{
  Node *node = nodes;
  while (node && node->id != id)
    node = node->next;
  return node;
}

void
label_set (Node *this, size_t value, char *name)
{
  uint32_t chain = djb_hash(name) % PRIME_1000;

  Label *label = this->chains[chain];

  while (label && !str_eq(label->name, name))
    label = label->next;

  if (!label)
  {
    label = NULL;
    ensure_malloc((label = malloc(sizeof(Label))));
    label->name = strf("%s", name);
    label->next = this->chains[chain];
    this->chains[chain] = label;
  }
  label->value = value;
}

Label*
label_get (Node *this, char *name)
{
  uint32_t chain = djb_hash(name) % PRIME_1000;

  Label *label = this->chains[chain];

  while (label && !str_eq(label->name, name))
    label = label->next;

  return label;
}

struct _Entry;

typedef void (*op_exec)(Node*,struct _Entry*);

typedef struct _Entry {
  char *name;
  op_exec exec;
  word value;
  struct _Entry *next;
} Entry;

typedef struct {
  Entry *chains[PRIME_1000];
} Dictionary;

Dictionary *dictionary;

static Entry*
lookup (char *name)
{
  uint32_t chain = djb_hash(name) % PRIME_1000;

  Entry *entry = dictionary->chains[chain];

  while (entry && !str_eq(entry->name, name))
    entry = entry->next;

  return entry;
}

static void
record (char *name, op_exec exec, word value)
{
  uint32_t chain = djb_hash(name) % PRIME_1000;

  Entry *entry = dictionary->chains[chain];

  while (entry && !str_eq(entry->name, name))
    entry = entry->next;

  if (!entry)
  {
    ensure_malloc((entry = malloc(sizeof(Entry))));
    entry->next = dictionary->chains[chain];
    dictionary->chains[chain] = entry;
    entry->name = strf("%s", name);
  }

  entry->exec = exec;
  entry->value = value;
}

static void
op_call (Node *this, Entry *entry)
{
  if (this->sp == STACK-1)
  {
    messagef(this, "call stack overflow");
    this->state = WTF;
    return;
  }

  this->stack[this->sp++] = this->script - this->code;
  this->script = this->code + this->array[this->current];
}

static void
op_jump (Node *this, Entry *entry)
{
  this->script = this->code + this->array[this->current];
}

static void
op_je (Node *this, Entry *entry)
{
  if (this->flags & FLAG_EQ)
    this->script = this->code + this->array[this->current];
}

static void
op_jl (Node *this, Entry *entry)
{
  if (this->flags & FLAG_LT)
    this->script = this->code + this->array[this->current];
}

static void
op_jg (Node *this, Entry *entry)
{
  if (this->flags & FLAG_GT)
    this->script = this->code + this->array[this->current];
}

static void
op_jn (Node *this, Entry *entry)
{
  if (!(this->flags & FLAG_EQ))
    this->script = this->code + this->array[this->current];
}

static int
can_return (Node *this)
{
  if (!this->sp)
  {
    messagef(this, "call stack underflow");
    this->state = WTF;
    return 0;
  }
  return 1;
}

static void
op_re (Node *this, Entry *entry)
{
  if (can_return(this) && this->flags & FLAG_EQ)
    this->script = this->code + this->stack[--this->sp];
}

static void
op_rn (Node *this, Entry *entry)
{
  if (can_return(this) && !(this->flags & FLAG_EQ))
    this->script = this->code + this->stack[--this->sp];
}

static void
op_rl (Node *this, Entry *entry)
{
  if (can_return(this) && this->flags & FLAG_EQ)
    this->script = this->code + this->stack[--this->sp];
}

static void
op_rg (Node *this, Entry *entry)
{
  if (can_return(this) && this->flags & FLAG_EQ)
    this->script = this->code + this->stack[--this->sp];
}

static void
op_cmp (Node *this, Entry *entry)
{
  this->flags =
      (this->array[this->previous] == this->array[this->current] ? FLAG_EQ: 0)
    | (this->array[this->previous] <  this->array[this->current] ? FLAG_LT: 0)
    | (this->array[this->previous] >  this->array[this->current] ? FLAG_GT: 0);
}

static void
op_self (Node *this, Entry *entry)
{
  this->array[this->current] = this->id;
}

static void
op_copy (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous];
}

static void
op_at (Node *this, Entry *entry)
{
  this->current = this->previous + this->array[this->current];

  if (this->current >= CELLS-1)
  {
    messagef(this, "out of bounds");
    this->state = WTF;
  }
}

static void
op_add (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] + this->array[this->current];
}

static void
op_inc (Node *this, Entry *entry)
{
  this->array[this->current]++;
}

static void
op_sub (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] - this->array[this->current];
}

static void
op_dec (Node *this, Entry *entry)
{
  this->array[this->current]--;
}

static void
op_mul (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] * this->array[this->current];
}

static void
op_div (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] / this->array[this->current];
}

static void
op_mod (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] % this->array[this->current];
}

static void
op_emit (Node *this, Entry *entry)
{
  printf("%c", (char)(this->array[this->current]));
}

static void
op_and (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] & this->array[this->current];
}

static void
op_or (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] | this->array[this->current];
}

static void
op_xor (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] ^ this->array[this->current];
}

static void
op_shl (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] << this->array[this->current];
}

static void
op_shr (Node *this, Entry *entry)
{
  this->array[this->current] = this->array[this->previous] >> this->array[this->current];
}

static void
op_neg (Node *this, Entry *entry)
{
  this->array[this->current] = -this->array[this->current];
}

static void
op_read (Node *this, Entry *entry)
{
  if (!this->qp)
  {
    this->script = this->mark;
    this->state  = PAUSE;
    this->ticks--;
    return;
  }

  free(this->receive);
  this->receive = this->queue[--this->qp];
  errorf("%ld <= (%ld)", this->id, this->receive[0]);
  this->array[this->current] = this->receive[0];
}

static void
op_send (Node *this, Entry *entry)
{
  Node *that = node_id(this->current);

  if (that->qp == QUEUE-1)
  {
    this->script = this->mark;
    this->state  = PAUSE;
    this->ticks--;
    return;
  }

  errorf("%ld => %ld (%ld)", this->id, that->id, this->compose[0]);

  that->queue[that->qp++] = this->compose;
  this->compose = NULL;
}

static void
op_msg (Node *this, Entry *entry)
{
  word length = this->array[this->current];

  free(this->compose);
  ensure_malloc((this->compose = malloc(sizeof(word) * (length+1))));
  this->compose[0] = length;

  for (word i = 0; i < length; i++)
    this->compose[i+1] = 0;
}

static void
op_set (Node *this, Entry *entry)
{
  word offset = this->array[this->current];
  word length = this->compose[0];

  ensure(offset >= 0 && offset < length) errorf("message access out of bounds");
  this->compose[offset] = this->array[this->previous];
}

static void
op_get (Node *this, Entry *entry)
{
  this->array[this->current] = this->receive[this->array[this->previous]+1];
}

static void
op_node (Node *this, Entry *entry)
{
  this->previous = this->current;
  this->current = entry->value;
}

static int
tick (Node *this)
{
  this->ticks = 0;

  if (this->state == PAUSE || this->qp > 0)
  {
    this->state = OK;

    char string[STRING];

    Entry *entry = NULL;

    while (this->state == OK && this->script[0])
    {
      this->mark = this->script;

      while (this->script[0] && isspace(this->script[0]))
        this->script++;

      // Comments
      if (this->script[0] == '(')
      {
        this->script++;
        int levels = 1;
        while (this->script[0] && levels > 0)
        {
          if (this->script[0] == '(') levels++;
          if (this->script[0] == ')') levels--;
          this->script++;
        }
        continue;
      }

      // Blocks
      if (this->script[0] == '{')
      {
        this->script++;
        char *block = this->script;
        int levels = 1;
        while (this->script[0] && levels > 0)
        {
          if (this->script[0] == '{') levels++;
          if (this->script[0] == '}') levels--;
          this->script++;
        }
        this->array[this->current] = block - this->code;
        continue;
      }

      size_t length = 0;

      while (this->script[0] && !isspace(this->script[0]))
      {
        string[length++] = this->script[0];
        this->script++;
      }

      string[length] = 0;
      Label *label = NULL;

      if (length == 0)
        break;

      if (this->skip)
      {
        this->skip--;
        continue;
      }

      if (string[0] == '}')
      {
        if (can_return(this))
          this->script = this->code + this->stack[--this->sp];
      }
      else
      // Character
      if (string[0] == '\'')
      {
        this->array[this->current] = string[1];
        this->ticks++;
      }
      else
      // Literal
      if (string[0] == '#')
      {
        this->array[this->current] = strtoll(&string[1], NULL, 0);
        this->ticks++;
      }
      else
      // Pointer
      if (isdigit(string[0]))
      {
        this->previous = this->current;
        this->current = strtoll(string, NULL, 0);
        this->ticks++;

        if (this->current >= CELLS-1)
        {
          messagef(this, "out of bounds");
          this->state = WTF;
        }
      }
      else
      if (string[0] == ':' && isalpha(string[1]))
      {
        label_set(this, this->current, &string[1]);
        this->ticks++;
      }
      else
      // Label
      if ((label = label_get(this, string)))
      {
        this->previous = this->current;
        this->current = label->value;
      }
      else
      // Word
      if ((entry = lookup(string)))
      {
        this->ticks++;
        entry->exec(this, entry);
      }
      else
      {
        errorf("what? %s", string);
        this->state = WTF;
      }
    }
    if (this->state != PAUSE)
    {
      this->script = this->code;
    }
  }
  return this->ticks;
}

int
main(int argc, char *argv[])
{
  ensure_malloc((dictionary = malloc(sizeof(Dictionary))));

  for (size_t chain = 0; chain < PRIME_1000; chain++)
    dictionary->chains[chain] = NULL;

  // stack_in, stack_out, is_in, is_out
  record("add",  op_add,  0);
  record("inc",  op_inc,  0);
  record("sub",  op_sub,  0);
  record("dec",  op_dec,  0);
  record("mul",  op_mul,  0);
  record("div",  op_div,  0);
  record("mod",  op_mod,  0);
  record("emit", op_emit, 0);
  record("copy", op_copy, 0);
  record("at",   op_at,   0);
  record("cmp",  op_cmp,  0);
  record("neg",  op_neg,  0);
  record("or",   op_or,   0);
  record("and",  op_and,  0);
  record("xor",  op_xor,  0);
  record("shl",  op_shl,  0);
  record("shr",  op_shr,  0);
  record("read", op_read, 0);
  record("send", op_send, 0);
  record("set",  op_set,  0);
  record("get",  op_get,  0);
  record("self", op_self, 0);
  record("call", op_call, 0);
  record("jump", op_jump, 0);
  record("je",   op_je,   0);
  record("jn",   op_jn,   0);
  record("jl",   op_jl,   0);
  record("jg",   op_jg,   0);
  record("re",   op_re,   0);
  record("rn",   op_rn,   0);
  record("rl",   op_rl,   0);
  record("rg",   op_rg,   0);

  DIR *dir = NULL;

  ensure((dir = opendir("."))) errorf("opendir");

  struct dirent *ent = NULL;

  word id = 1;

  while ((ent = readdir(dir)))
  {
    size_t len = strlen(ent->d_name);
    if (len > 5 && str_eq(".node", ent->d_name + len - 5))
    {
      errorf("loading %s", ent->d_name);

      char *name = strf("%s", ent->d_name);
      name[len-5] = 0;

      struct stat st;
      if (stat(ent->d_name, &st) == 0)
      {
        char *code = NULL;
        ensure_malloc((code = malloc(st.st_size+1)));
        int fd = open(ent->d_name, O_RDONLY);
        if (fd >= 0)
        {
          if (read(fd, code, st.st_size) == st.st_size)
          {
            code[st.st_size] = 0;

            node_new(id, name, code);
            record(name, op_node, id);
            id++;
          }
        }
        close(fd);
        free(code);
      }
      free(name);
    }
  }
  closedir(dir);

  for (int argi = 1; argi < argc; argi++)
  {
    Entry *entry = NULL;
    if ((entry = lookup(argv[argi])))
    {
      errorf("run %s", argv[argi]);
      Node *this = node_id(entry->value);
      this->state = PAUSE;
      tick(this);
    }
  }

  int done = 0;
  Node *node = NULL;

  while (!done)
  {
    word ticks = 0;

    node = nodes;
    while (node)
    {
      ticks += tick(node);

      if (node->state == PAUSE)
      {
        errorf("%s %s", node->name, node->message);
      }
      else
      if (node->state == WTF)
      {
        errorf("%s", node->message);
        done = 1;
      }
      node = node->next;
    }
    if (!done)
    {
      word inputs = 0;
      word paused = 0;

      node = nodes;
      while (node)
      {
        inputs += node->qp > 0 ? 1:0;
        paused += node->state == PAUSE ? 1:0;
        node = node->next;
      }

      done = (!inputs && !paused) || !ticks;
    }
  }

  int state = EXIT_SUCCESS;

  node = nodes;
  while (node)
  {
    if (node->state == PAUSE)
    {
      errorf("%s paused", node->name);
    }

    if (node->state == WTF)
    {
      state = EXIT_FAILURE;
      errorf("%s aborted", node->name);
    }

    node = node->next;
  }

  while (nodes)
  {
    Node *next = nodes->next;
    node_free(nodes);
    nodes = next;
  }

  return state;
}
