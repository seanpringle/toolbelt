#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PRIME_1000 997
#define PRIME_10000 9973
#define PRIME_100000 99991
#define PRIME_1000000 999983

#define ensure(x) for ( ; !(x) ; exit(EXIT_FAILURE) )
#define errorf(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while(0)

#define min(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a: _b; })
#define max(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a: _b; })

typedef uint8_t byte_t;

void*
allocate (size_t bytes)
{
  void *ptr = malloc(bytes);
  ensure(ptr) errorf("malloc failed %lu bytes", bytes);
  return ptr;
}

void*
reallocate (void *ptr, size_t bytes)
{
  ptr = realloc(ptr, bytes);
  ensure(ptr) errorf("malloc failed %lu bytes", bytes);
  return ptr;
}

int
regmatch (regex_t *re, char *subject)
{
  return regexec(re, subject, 0, NULL, 0) == 0;
}

uint32_t
djb_hash (char *str)
{
  uint32_t hash = 5381;
  for (int i = 0; str[i]; hash = hash * 33 + str[i++]);
  return hash;
}

#include "c/time.c"
#include "c/str.c"
#include "c/text.c"
#include "c/file.c"
#include "c/array.c"
#include "c/vector.c"
#include "c/list.c"
#include "c/map.c"
#include "c/json.c"
#include "c/pool.c"
#include "c/db.c"
#include "c/thread.c"

#include <sys/wait.h>

#define PIPE_READ 0
#define PIPE_WRITE 1

// execute sub-process and connect its stdin=infp and stdout=outfp
pid_t
exec_cmd_io(const char *command, int *infp, int *outfp, int *errfp)
{
  int p_stdin[2], p_stdout[2], p_stderr[2];

  if (pipe(p_stdin) != 0 || pipe(p_stdout) != 0 || pipe(p_stderr) != 0)
    return -1;

  pid_t pid = fork();

  if (pid < 0)
    return pid;

  if (pid == 0)
  {
    close(p_stdin[PIPE_WRITE]);
    dup2(p_stdin[PIPE_READ], STDIN_FILENO);

    close(p_stdout[PIPE_READ]);
    dup2(p_stdout[PIPE_WRITE], STDOUT_FILENO);

    close(p_stderr[PIPE_READ]);
    dup2(p_stderr[PIPE_WRITE], STDERR_FILENO);

    execlp("/bin/sh", "sh", "-c", command, NULL);
    exit(EXIT_FAILURE);
  }

  if (infp == NULL)
    close(p_stdin[PIPE_WRITE]);
  else
    *infp = p_stdin[PIPE_WRITE];

  if (outfp == NULL)
    close(p_stdout[PIPE_READ]);
  else
    *outfp = p_stdout[PIPE_READ];

  if (errfp == NULL)
    close(p_stderr[PIPE_READ]);
  else
    *errfp = p_stderr[PIPE_READ];

  close(p_stdin[PIPE_READ]);
  close(p_stdout[PIPE_WRITE]);
  close(p_stderr[PIPE_WRITE]);

  return pid;
}

int
command (const char *cmd, const char *data, char **output, char **errput)
{
  int status = EXIT_SUCCESS;
  int in, out, err;

  pid_t pid = exec_cmd_io(cmd, &in, &out, &err);

  if (pid <= 0)
  {
    status = EXIT_FAILURE;
    goto done;
  }

  if (data && write(in, data, strlen(data)) != strlen(data))
  {
    status = EXIT_FAILURE;
    close(in);
    close(out);
    close(err);
    kill(pid, SIGTERM);
    goto done;
  }

  close(in);

  int outlen = 0;
  char *outres = malloc(1024);
  int errlen = 0;
  char *errres = malloc(1024);
  for (;;)
  {
    int orc = read(out, outres+outlen, 1023);
    if (orc > 0) outlen += orc;
    outres = realloc(outres, outlen+1024);

    int erc = read(err, errres+errlen, 1023);
    if (erc > 0) errlen += erc;
    errres = realloc(errres, errlen+1024);

    if (!orc && !erc) break;
  }
  outres[outlen] = 0;
  errres[errlen] = 0;

  if (output) *output = outres; else free(outres);
  if (errput) *errput = errres; else free(errres);

  close(out);
  close(err);

  waitpid(pid, &status, 0);

done:
  return status;
}

#include <sys/socket.h>
#include <sys/un.h>

typedef int (*socket_serve_cb)(int);

int
socket_serve (const char *path, socket_serve_cb cb)
{
  int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);

  if (sock_fd < 0)
    return EXIT_FAILURE;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  unlink(path);

  if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    return EXIT_FAILURE;

  if (listen(sock_fd, 32) != 0)
    return EXIT_FAILURE;

  int fd;

  while ((fd = accept(sock_fd, NULL, NULL)) && fd >= 0 && cb(fd) == EXIT_SUCCESS);

  close(sock_fd);
  return EXIT_SUCCESS;
}
