#ifdef TOOLBELT_THREAD

#include <pthread.h>

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

int
thread_join (thread_t *thread)
{
  int ok = 1;

  if (thread->started && !thread->joined)
  {
    pthread_mutex_unlock(&thread->mutex);
    int rc = pthread_join(thread->pthread, NULL);
    pthread_mutex_lock(&thread->mutex);
    if (rc == 0)
      thread->joined = 1;
    else
    {
      errorf("thread_join: %d", rc);
      ok = 0;
    }
  }
  return ok;
}

thread_t*
thread_new ()
{
  pthread_mutex_lock(&all_threads_mutex);

  vector_each(all_threads, thread_t *t)
  {
    pthread_mutex_lock(&t->mutex);
    if (t->stopped)
    {
      thread_join(t);
      pthread_mutex_unlock(&t->mutex);
      pthread_mutex_unlock(&all_threads_mutex);
      return t;
    }
    pthread_mutex_unlock(&t->mutex);
  }

  thread_t *thread = allocate(sizeof(thread_t));
  memset(thread, 0, sizeof(thread_t));
  pthread_mutex_init(&thread->mutex, NULL);
  pthread_cond_init(&thread->cond, NULL);

  thread->id = vector_count(all_threads);
  vector_push(all_threads, thread);

  pthread_mutex_unlock(&all_threads_mutex);
  return thread;
}

void*
thread_run (void *ptr)
{
  thread_t *thread = (thread_t*)ptr;
  pthread_setspecific(_key_self, thread);

  int error = thread->main(thread->payload);

  pthread_mutex_lock(&thread->mutex);
  thread->error = error;
  thread->stopped = 1;
  pthread_mutex_unlock(&thread->mutex);
  return NULL;
}

int
thread_start (thread_t *thread, thread_main main, void *payload)
{
  pthread_mutex_lock(&thread->mutex);

  thread_join(thread);

  thread->main    = main;
  thread->payload = payload;
  thread->started = 1;
  thread->stopped = 0;
  thread->joined  = 0;
  thread->waiting = 0;

  pthread_mutex_unlock(&thread->mutex);

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
  pthread_mutex_lock(&thread->mutex);
  thread_join(thread);
  int rc = thread->error;
  pthread_mutex_unlock(&thread->mutex);
  return rc;
}

void
thread_free (thread_t *thread)
{
  pthread_mutex_destroy(&thread->mutex);
  pthread_cond_destroy(&thread->cond);
  free(thread);
}

int
thread_running (thread_t *thread)
{
  pthread_mutex_lock(&thread->mutex);
  int running = !thread->stopped;
  pthread_mutex_unlock(&thread->mutex);
  return running;
}

void
multithreaded ()
{
  pthread_key_create(&_key_self, NULL);
  pthread_mutex_init(&all_threads_mutex, NULL);
  all_threads = vector_new();

  pthread_setspecific(_key_self, thread_new());
  self->started = 1;
}

void
singlethreaded ()
{
  pthread_mutex_lock(&all_threads_mutex);

  vector_each(all_threads, thread_t *thread)
  {
    pthread_mutex_lock(&thread->mutex);
    thread_join(thread);
    thread_free(thread);
  }
  vector_free(all_threads);
  all_threads = NULL;

  free(self);

  int rc = pthread_key_delete(_key_self);

  if (rc != 0)
    errorf("singlethreaded (key delete): %d", rc);

  pthread_mutex_destroy(&all_threads_mutex);
}

channel_t*
channel_new (size_t limit)
{
  channel_t *channel = allocate(sizeof(channel_t));
  pthread_mutex_init(&channel->mutex, NULL);
  pthread_cond_init(&channel->write_cond, NULL);
  channel->queue = list_new();
  channel->readers = list_new();
  channel->handled = 0;
  channel->limit = limit;
  return channel;
}

void
channel_free (channel_t *channel)
{
  pthread_mutex_lock(&channel->mutex);
  pthread_mutex_destroy(&channel->mutex);
  pthread_cond_destroy(&channel->write_cond);
  list_free(channel->queue);
  list_free(channel->readers);
  free(channel);
}

void
channel_write (channel_t *channel, void *ptr)
{
  pthread_mutex_lock(&channel->mutex);

  channel->handled++;

  int dispatched = 0;

  while (!dispatched && list_count(channel->readers))
  {
    list_each(channel->readers, thread_t *thread)
    {
      pthread_mutex_lock(&thread->mutex);

      if (thread->waiting)
      {
        thread->channel = channel;
        thread->ptr     = ptr;
        thread->waiting = 0;
        list_del(channel->readers, loop.index);
        pthread_cond_signal(&thread->cond);
        pthread_mutex_unlock(&thread->mutex);
        dispatched = 1;
        break;
      }
      pthread_mutex_unlock(&thread->mutex);
    }
    if (!dispatched)
    {
      pthread_mutex_unlock(&channel->mutex);
      usleep(1);
      pthread_mutex_lock(&channel->mutex);
    }
  }
  if (!dispatched)
  {
    while (channel->limit > 0 && channel->limit < list_count(channel->queue))
      pthread_cond_wait(&channel->write_cond, &channel->mutex);

    list_push(channel->queue, ptr);
  }
  pthread_mutex_unlock(&channel->mutex);
}

void
channel_broadcast (channel_t *channel, void *ptr)
{
  pthread_mutex_lock(&channel->mutex);

  list_each(channel->readers, thread_t *thread)
  {
    pthread_mutex_lock(&thread->mutex);
    if (thread->waiting)
    {
      thread->channel = channel;
      thread->ptr     = ptr;
      thread->waiting = 0;
      pthread_cond_signal(&thread->cond);
    }
    pthread_mutex_unlock(&thread->mutex);
  }
  list_clear(channel->readers);
  pthread_mutex_unlock(&channel->mutex);
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
    pthread_mutex_unlock(&channel->mutex);
  }
  return ptr;
}

list_t*
channel_consume (channel_t *channel)
{
  list_t *consume = NULL;
  pthread_mutex_lock(&channel->mutex);
  if (list_count(channel->queue))
  {
    consume = channel->queue;
    channel->queue = list_new();
    pthread_cond_signal(&channel->write_cond);
  }
  pthread_mutex_unlock(&channel->mutex);
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
    pthread_mutex_lock(&channel->mutex);
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
    pthread_mutex_unlock(&channel->mutex);
    if (ptr) break;
  }

  if (!ptr)
  {
    pthread_mutex_lock(&self->mutex);
    self->waiting = 1;
    pthread_cond_wait(&self->cond, &self->mutex);
    pthread_mutex_unlock(&self->mutex);
    ptr = self->ptr;
    from = self->channel;
  }

  list_each(channels, channel_t *channel)
  {
    if (loop.index == waiting_channels) break;
    if (channel == from) continue;

    pthread_mutex_lock(&channel->mutex);
    list_each(channel->readers, thread_t *thread)
    {
      if (thread == self)
      {
        list_del(channel->readers, loop.index);
        break;
      }
    }
    pthread_mutex_unlock(&channel->mutex);
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
  pthread_mutex_lock(&channel->mutex);
  size_t backlog = list_count(channel->queue);
  pthread_mutex_unlock(&channel->mutex);
  return backlog;
}

size_t
channel_readers (channel_t *channel)
{
  pthread_mutex_lock(&channel->mutex);
  size_t readers = list_count(channel->readers);
  pthread_mutex_unlock(&channel->mutex);
  return readers;
}

size_t
channel_handled (channel_t *channel)
{
  pthread_mutex_lock(&channel->mutex);
  size_t handled = channel->handled;
  pthread_mutex_unlock(&channel->mutex);
  return handled;
}

#endif