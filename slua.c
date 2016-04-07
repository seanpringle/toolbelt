
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#define ensure(x) for ( ; !(x) ; exit(EXIT_FAILURE) )
#define errorf(...) ({ fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); })

int
main(int argc, char const *argv[])
{
  const char *socket_path = "/tmp/slua.sock";
  const char *script_path = "test.lua";

  lua_State *lua = luaL_newstate();
  luaL_openlibs(lua);

  int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);

  ensure(sock_fd >= 0)
    errorf("socket failed");

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  unlink(socket_path);

  ensure(bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
    errorf("bind failed");

  ensure(listen(sock_fd, 32) == 0)
    errorf("listen failed");

  int fd;

  while ((fd = accept(sock_fd, NULL, NULL)) && fd >= 0)
  {
    pid_t pid = fork();

    if (pid > 0)
    {
      close(fd);
      continue;
    }

    if (pid < 0)
    {
      errorf("fork failed");
      close(fd);
      continue;
    }

    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);

    int rc = EXIT_SUCCESS;

    if (luaL_dofile(lua, script_path) != 0)
    {
      errorf("lua error: %s", lua_tostring(lua, -1));
      rc = EXIT_FAILURE;
    }

    close(fd);
    lua_close(lua);
    exit(rc);
  }

  close(sock_fd);

  return EXIT_SUCCESS;
}
