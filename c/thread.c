#ifdef TOOLBELT_THREAD

#include <pthread.h>

#define mutex_lock(m) assert0(pthread_mutex_lock((m)))
#define mutex_unlock(m) assert0(pthread_mutex_unlock((m)))

typedef int (*thread_main)(void*);

typedef struct _channel_t {
  pthread_mutex_t mutex;
  pthread_cond_t write_cond;
  list_t *queue;
  list_t *readers;
  size_t handled;
  size_t limit;
} channel_t;

typedef struct _thread_t {
  pthread_t pthread;
  pthread_mutex_t mutex;
  thread_main main;
  void *payload;
  off_t id;
  int started;
  int stopped;
  int joined;
  int error;
  int waiting;
  pthread_cond_t cond;
  channel_t *channel;
  void *ptr;
} thread_t;

static pthread_key_t _key_self;
#define self ((thread_t*)pthread_getspecific(_key_self))

pthread_mutex_t all_threads_mutex;
vector_t *all_threads;

pthread_mutex_t mutex_stderr;

int
thread_join (thread_t *thread)
{
  int rs = 0;

  if (thread->started && !thread->joined)
  {
    mutex_unlock(&thread->mutex);
    int rc = pthread_join(thread->pthread, NULL);
    mutex_lock(&thread->mutex);
    if (rc == 0)
      thread->joined = 1;
    else
    {
      errorf("thread_join: %d", rc);
      rs = 1;
    }
  }
  return rs;
}

thread_t*
thread_new ()
{
  mutex_lock(&all_threads_mutex);

  vector_each(all_threads, thread_t *t)
  {
    mutex_lock(&t->mutex);
    if (t->stopped && thread_join(t) == 0)
    {
      mutex_unlock(&t->mutex);
      mutex_unlock(&all_threads_mutex);
      return t;
    }
    mutex_unlock(&t->mutex);
  }

  thread_t *thread = allocate(sizeof(thread_t));
  memset(thread, 0, sizeof(thread_t));
  assert0(pthread_mutex_init(&thread->mutex, NULL));
  assert0(pthread_cond_init(&thread->cond, NULL));

  thread->id = vector_count(all_threads);
  vector_push(all_threads, thread);

  mutex_unlock(&all_threads_mutex);
  return thread;
}

void*
thread_run (void *ptr)
{
  thread_t *thread = (thread_t*)ptr;
  assert0(pthread_setspecific(_key_self, thread));

  int error = thread->main(thread->payload);

  mutex_lock(&thread->mutex);
  thread->error = error;
  thread->stopped = 1;
  mutex_unlock(&thread->mutex);
  return NULL;
}

int
thread_start (thread_t *thread, thread_main main, void *payload)
{
  mutex_lock(&thread->mutex);

  if (thread_join(thread) != 0)
    return EXIT_FAILURE;

  thread->main    = main;
  thread->payload = payload;
  thread->started = 1;
  thread->stopped = 0;
  thread->joined  = 0;
  thread->waiting = 0;

  mutex_unlock(&thread->mutex);

  int rc = pthread_create(&thread->pthread, NULL, thread_run, thread);

  if (rc)
  {
    errorf("thread_start (create): %d", rc);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int
thread_wait (thread_t *thread)
{
  mutex_lock(&thread->mutex);
  int rc = thread_join(thread);
  if (!rc) rc = thread->error;
  mutex_unlock(&thread->mutex);
  return rc;
}

void
thread_free (thread_t *thread)
{
  assert0(pthread_mutex_destroy(&thread->mutex));
  assert0(pthread_cond_destroy(&thread->cond));
  free(thread);
}

int
thread_running (thread_t *thread)
{
  mutex_lock(&thread->mutex);
  int running = !thread->stopped;
  mutex_unlock(&thread->mutex);
  return running;
}

void
errorf_multithreaded (char *pattern, ...)
{
  mutex_lock(&mutex_stderr);
  va_list args;
  va_start(args, pattern);
  vfprintf(stderr, "thread %u: ", self->id);
  vfprintf(stderr, pattern, args);
  fputc('\n', stderr);
  va_end(args);
  mutex_unlock(&mutex_stderr);
}

void
multithreaded ()
{
  assert0(pthread_key_create(&_key_self, NULL));
  assert0(pthread_mutex_init(&all_threads_mutex, NULL));
  all_threads = vector_new();

  assert0(pthread_setspecific(_key_self, thread_new()));
  self->started = 1;

  assert0(pthread_mutex_init(&mutex_stderr, NULL));
  errorf_handler = errorf_multithreaded;
}

void
singlethreaded ()
{
  mutex_lock(&all_threads_mutex);

  vector_each(all_threads, thread_t *thread)
  {
    if (thread != self)
    {
      mutex_lock(&thread->mutex);
      assert0(thread_join(thread));
      mutex_unlock(&thread->mutex);
      thread_free(thread);
    }
  }
  vector_free(all_threads);
  all_threads = NULL;

  errorf_handler = errorf_default;
  assert0(pthread_mutex_destroy(&mutex_stderr));

  thread_free(self);
  assert0(pthread_key_delete(_key_self));

  mutex_unlock(&all_threads_mutex);
  assert0(pthread_mutex_destroy(&all_threads_mutex));
}

channel_t*
channel_new (size_t limit)
{
  channel_t *channel = allocate(sizeof(channel_t));
  assert0(pthread_mutex_init(&channel->mutex, NULL));
  assert0(pthread_cond_init(&channel->write_cond, NULL));
  channel->queue = list_new();
  channel->readers = list_new();
  channel->handled = 0;
  channel->limit = limit;
  return channel;
}

void
channel_free (channel_t *channel)
{
  assert0(pthread_mutex_destroy(&channel->mutex));
  assert0(pthread_cond_destroy(&channel->write_cond));
  list_free(channel->queue);
  list_free(channel->readers);
  free(channel);
}

void
channel_write (channel_t *channel, void *ptr)
{
  mutex_lock(&channel->mutex);

  channel->handled++;

  int dispatched = 0;

  while (!dispatched && list_count(channel->readers))
  {
    list_each(channel->readers, thread_t *thread)
    {
      mutex_lock(&thread->mutex);

      if (thread->waiting)
      {
        thread->channel = channel;
        thread->ptr     = ptr;
        thread->waiting = 0;
        list_del(channel->readers, loop.index);
        pthread_cond_signal(&thread->cond);
        mutex_unlock(&thread->mutex);
        dispatched = 1;
        break;
      }
      mutex_unlock(&thread->mutex);
    }
    if (!dispatched)
    {
      mutex_unlock(&channel->mutex);
      usleep(1);
      mutex_lock(&channel->mutex);
    }
  }
  if (!dispatched)
  {
    while (channel->limit > 0 && channel->limit < list_count(channel->queue))
      pthread_cond_wait(&channel->write_cond, &channel->mutex);

    list_push(channel->queue, ptr);
  }
  mutex_unlock(&channel->mutex);
}

void
channel_broadcast (channel_t *channel, void *ptr)
{
  mutex_lock(&channel->mutex);

  list_each(channel->readers, thread_t *thread)
  {
    mutex_lock(&thread->mutex);
    if (thread->waiting)
    {
      thread->channel = channel;
      thread->ptr     = ptr;
      thread->waiting = 0;
      pthread_cond_signal(&thread->cond);
    }
    mutex_unlock(&thread->mutex);
  }
  list_clear(channel->readers);
  mutex_unlock(&channel->mutex);
}

void*
channel_try_read (channel_t *channel)
{
  void *ptr = NULL;
  if (pthread_mutex_trylock(&channel->mutex) == 0)
  {
    if (list_count(channel->queue))
    {
      ptr = list_shift(channel->queue);
      pthread_cond_signal(&channel->write_cond);
    }
    mutex_unlock(&channel->mutex);
  }
  return ptr;
}

list_t*
channel_consume (channel_t *channel)
{
  list_t *consume = NULL;
  mutex_lock(&channel->mutex);
  if (list_count(channel->queue))
  {
    consume = channel->queue;
    channel->queue = list_new();
    pthread_cond_signal(&channel->write_cond);
  }
  mutex_unlock(&channel->mutex);
  return consume;
}

void*
channel_multi_read (channel_t **selected, list_t *channels)
{
  void *ptr = NULL;
  channel_t *from = NULL;

  int waiting_channels = 0;
  list_each(channels, channel_t *channel)
  {
    mutex_lock(&channel->mutex);
    if (list_count(channel->queue))
    {
      ptr = list_shift(channel->queue);
      pthread_cond_signal(&channel->write_cond);
      from = channel;
    }
    else
    {
      list_push(channel->readers, self);
      waiting_channels++;
    }
    mutex_unlock(&channel->mutex);
    if (ptr) break;
  }

  if (!ptr)
  {
    mutex_lock(&self->mutex);
    self->waiting = 1;
    pthread_cond_wait(&self->cond, &self->mutex);
    mutex_unlock(&self->mutex);
    ptr = self->ptr;
    from = self->channel;
  }

  list_each(channels, channel_t *channel)
  {
    if (loop.index == waiting_channels) break;
    if (channel == from) continue;

    mutex_lock(&channel->mutex);
    list_each(channel->readers, thread_t *thread)
    {
      if (thread == self)
      {
        list_del(channel->readers, loop.index);
        break;
      }
    }
    mutex_unlock(&channel->mutex);
  }

  if (selected) *selected = from;
  return ptr;
}

void*
channel_select (channel_t **selected, int n, ...)
{
  va_list args;
  va_start(args, n);

  list_t *channels = list_new();

  for (int i = 0; i < n; i++)
    list_push(channels, va_arg(args, channel_t*));

  va_end(args);

  void *ptr = channel_multi_read(selected, channels);

  list_free(channels);

  return ptr;
}

void*
channel_read (channel_t *channel)
{
  return channel_select(NULL, 1, channel);
}

size_t
channel_backlog (channel_t *channel)
{
  mutex_lock(&channel->mutex);
  size_t backlog = list_count(channel->queue);
  mutex_unlock(&channel->mutex);
  return backlog;
}

size_t
channel_readers (channel_t *channel)
{
  mutex_lock(&channel->mutex);
  size_t readers = list_count(channel->readers);
  mutex_unlock(&channel->mutex);
  return readers;
}

size_t
channel_handled (channel_t *channel)
{
  mutex_lock(&channel->mutex);
  size_t handled = channel->handled;
  mutex_unlock(&channel->mutex);
  return handled;
}

#endif
