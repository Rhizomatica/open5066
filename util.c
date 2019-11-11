/* util.c  -  Utility functions
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * See file COPYING.
 *
 * 15.4.2006, created over Easter holiday --Sampo
 */

#include "errmac.h"
#include "afr.h"

#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#if !defined(DSSTDIO) && !defined(MINGW)
/* *** Static initialization of struct flock is suspect since man fcntl() documentation
 * does not guarantee ordering of the fields, or that they would be the first fields.
 * On Linux-2.4 and 2.6 as well as Solaris-8 the ordering is as follows, but this needs
 * to be checked on other platforms.
 *                       l_type,  l_whence, l_start, l_len */
struct flock ds_rdlk = { F_RDLCK, SEEK_SET, 0, 0 };
struct flock ds_wrlk = { F_WRLCK, SEEK_SET, 0, 0 };
struct flock ds_unlk = { F_UNLCK, SEEK_SET, 0, 0 };
#endif

/* Low level function that keeps on sucking from a file descriptor until
 * want is satisfied or error happens. */

int read_all_fd(int fd, char* p, int want, int* got_all)
{
#ifdef DSSTDIO
  int got;
  got = fread(p, 1, want, (FILE*)fd);
  if (got_all) *got_all = got;
#elif defined(MINGW)
  DWORD got;
  if (!ReadFile((HANDLE)fd, p, want, &got, 0))
    return -1;
  if (got_all) *got_all = got;
#else
  int got = 0;
  if (got_all) *got_all = 0;
  while (want) {
    got = read(fd, p, want);
    if (got <= 0) break;  /* EOF, possibly premature */
    if (got_all) *got_all += got;
    p += got;
    want -= got;
  }
#endif
  return got;
}

int write_all_fd(int fd, char* p, int pending)
{
#ifdef MINGW
  DWORD wrote;
  if (!fd || !pending || !p) return 0;  
  if (!WriteFile((HANDLE)fd, p, pending, &wrote, 0))
    return 0;
  FlushFileBuffers((HANDLE)fd);
  D("write_all_fd(%x, `%.*s', %d) wrote=%d\n", fd, pending, p, pending, wrote);
#else
  int wrote;
  if ((fd < 0) || !pending || !p) return 0;
  while (pending) {
    wrote = write(fd, p, pending);
    if (wrote <= 0) return 0;
    pending -= wrote;
    p += wrote;
  }
#endif
  return 1;
}

int write_or_append_lock_c_path(char* c_path, char* data, int len, CU8* which, int seeky, int flag)
{
  int fd;
  if (!c_path || !data)
    return 0;
#ifdef MINGW
  fd = CreateFile(c_path, MINGW_RW_PERM, 0 /* 0  means no sharing allowed */, 0 /* security */,
		  (flag == O_APPEND) ? OPEN_ALWAYS : CREATE_ALWAYS /* truncates, too? */,
		  FILE_ATTRIBUTE_NORMAL, 0);
  if (!fd) goto badopen;
  if (flag == O_APPEND) {
    MS_LONG zero = 0;
    SetFilePointer(fd, 0, &zero, FILE_END);  /* seek to end */
  }

  if (!write_all_fd(fd, data, len)) {
    ERR("%s: Writing to file `%s' %d bytes failed: %d %s. Check permissions and disk space.", which, c_path, len, errno, STRERROR(errno));
    return 0;
  }
  
#else
  fd = open(c_path, O_WRONLY | O_CREAT | flag, 0666);
  if (fd == -1) goto badopen;
  if (
#ifdef USE_LOCK
      flock(fd, LOCK_EX)
#else
      lockf(fd, F_LOCK, 0)
#endif
      == -1) {
    ERR("%s: Locking exclusively file `%s' failed: %d %s. Check permissions and that the file system supports locking.", which, c_path, errno, STRERROR(errno));
    close(fd);
    return 0;
  }
  
  lseek(fd,0,seeky);
  if (!write_all_fd(fd, data, len)) {
    ERR("%s: Writing to file(%s) %d bytes failed: %d %s. Check permissions and disk space.", which, c_path, len, errno, STRERROR(errno));
#ifdef USE_LOCK
    flock(fd, LOCK_UN);
#else
    lockf(fd, F_ULOCK, 0);
#endif
    return 0;
  }
  
#ifdef USE_LOCK
  flock(fd, LOCK_UN);
#else
  lockf(fd, F_ULOCK, 0);
#endif
#endif
  if (close(fd) < 0) {
    ERR("%s: closing file(%s) after writing %d bytes failed: %d %s. Check permissions and disk space. Could be NFS problem.", which, c_path, len, errno, STRERROR(errno));
    return 0;
  }
  return 1;
badopen:
  ERR("%s: Opening file(%s) for writing failed: %d %s. Check permissions.", which, c_path, errno, STRERROR(errno));
  return 0;
}

int hexdump(char* msg, char* p, char* lim, int max)
{
  int i;
  char* lim16;
  char buf[3*16+1+1+16+1];
  if (!msg)
    msg = "";
  if (lim-p > max)
    lim = p + max;
  
  buf[sizeof(buf)-1] = '\0';
  
  while (p<lim) {
    memset(buf, ' ', sizeof(buf)-1);
    lim16 = MIN(p+16, lim);
    for (i = 0; p<lim16; ++p, ++i) {
      buf[3*i+(i>7?1:0)]   = HEX_DIGIT((*p >> 4) & 0x0f);
      buf[3*i+1+(i>7?1:0)] = HEX_DIGIT(*p & 0x0f);
      switch (*p) {
      case '\0': buf[3*16+1+1+i] = '~'; break;
      case '\r': buf[3*16+1+1+i] = '['; break;
      case '\n': buf[3*16+1+1+i] = ']'; break;
      case '~':  buf[3*16+1+1+i] = '^'; break;
      case '[':  buf[3*16+1+1+i] = '^'; break;
      case ']':  buf[3*16+1+1+i] = '^'; break;
      default:
	buf[3*16+1+1+i] = *p < ' ' ? '^' : *p;
      }
    }
    fprintf(stderr, "%s%s\n", msg, buf);
  }
  return 0;
}

/* EOF  --  util.c */
