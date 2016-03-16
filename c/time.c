#include <time.h>
#include <sys/time.h>

uint64_t
ustamp ()
{
  struct timeval tv;
  gettimeofday(&tv,NULL);
  return 1000000 * tv.tv_sec + tv.tv_usec;
}

double
utime ()
{
  return (double)ustamp() / 1000000;
}
