/* ====================================================================
 * Copyright (c) 1995-1997 The Apache Group.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the Apache Group
 *    for use in the Apache HTTP server project (http://www.apache.org/)."
 *
 * 4. The names "Apache Server" and "Apache Group" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * 5. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the Apache Group
 *    for use in the Apache HTTP server project (http://www.apache.org/)."
 *
 * THIS SOFTWARE IS PROVIDED BY THE APACHE GROUP ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE APACHE GROUP OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Group and was originally based
 * on public domain software written at the National Center for
 * Supercomputing Applications, University of Illinois, Urbana-Champaign.
 * For more information on the Apache Group and the Apache HTTP server
 * project, please see <http://www.apache.org/>.
 *
 */

/*
 * conf.h: system-dependant #defines and includes...
 * See README for a listing of what they mean
 */


#ifdef WIN32
#include "../os/win32/os.h"
#else
#include "os.h"
#endif

#if !defined(QNX) && !defined(MPE) && !defined(WIN32)
#include <sys/param.h>
#endif

/* Define one of these according to your system. */
#if defined(MINT)
typedef int rlim_t;
#define HAVE_SYS_RESOURCE_H
#define JMP_BUF sigjmp_buf
#define NO_LONG_DOUBLE
#define USE_FLOCK_SERIALIZED_ACCEPT
#define _BSD_SOURCE
#define EAGAIN EACCESS
int initgroups (char *, int);     
char *crypt (const char *pw, const char *salt);
int gethostname (char *name, int namelen);

#elif defined(MPE)
#include <sys/times.h>
#define NO_SETSID
#define NO_KILLPG
#define NO_WRITEV
#define NEED_INITGROUPS
#define NEED_STRCASECMP
#define NEED_STRDUP
#define NEED_STRNCASECMP
extern void GETPRIVMODE();
extern void GETUSERMODE();
extern char *inet_ntoa();
#define NO_SLACK
#define NO_GETTIMEOFDAY

#elif defined(SUNOS4)
#define HAVE_GMTOFF
#define HAVE_SYS_RESOURCE_H
#undef NO_KILLPG
#undef NO_SETSID
char *crypt(const char *pw, const char *salt);
char *mktemp(char *template);
#define HAVE_MMAP
#define USE_MMAP_FILES
#include <sys/time.h>
#define NEED_STRERROR
typedef int rlim_t;
#define memmove(a,b,c) bcopy(b,a,c)
#define NO_LINGCLOSE
#define USE_FLOCK_SERIALIZED_ACCEPT
#define NEED_DIFFTIME
#define HAVE_SYSLOG

#elif defined(SOLARIS2)
#undef HAVE_GMTOFF
#define NO_KILLPG
#undef NO_SETSID
#define HAVE_SYS_RESOURCE_H
#define bzero(a,b) memset(a,0,b)
/*#define USE_FCNTL_SERIALIZED_ACCEPT */
/*#define USE_SYSVSEM_SERIALIZED_ACCEPT */
#if SOLARIS2 < 250
#define USE_FCNTL_SERIALIZED_ACCEPT
#else
#define USE_PTHREAD_SERIALIZED_ACCEPT
#endif
#define NEED_UNION_SEMUN
#define HAVE_MMAP
#define USE_MMAP_FILES
#define HAVE_CRYPT_H
int gethostname(char *name, int namelen);
#define HAVE_SYSLOG

#elif defined(IRIX)
#undef HAVE_GMTOFF
/* IRIX has killpg, but it's only in _BSD_COMPAT, so don't use it in case
 * there's some weird conflict with non-BSD signals */
#define NO_KILLPG
#undef NO_SETSID
#if !defined(USE_FCNTL_SERIALIZED_ACCEPT) && !defined(USE_USLOCK_SERIALIZED_ACCEPT)
#define USE_SYSVSEM_SERIALIZED_ACCEPT
#endif
#define HAVE_SHMGET
#define USE_MMAP_FILES
#define HAVE_CRYPT_H
#define NO_LONG_DOUBLE
#define HAVE_BSTRING_H
#define NO_LINGCLOSE
#define HAVE_SYSLOG

#elif defined(HIUX)
#define HAVE_SYS_RESOURCE_H
#undef HAVE_GMTOFF
#define NO_KILLPG
#undef NO_SETSID
#ifndef _HIUX_SOURCE
#define _HIUX_SOURCE
#endif
#define HAVE_SHMGET
#define SELECT_NEEDS_CAST
#define HAVE_SYSLOG

#elif defined(HPUX) || defined(HPUX10)
#define HAVE_SYS_RESOURCE_H
#undef HAVE_GMTOFF
#define NO_KILLPG
#undef NO_SETSID
#ifndef _HPUX_SOURCE
#define _HPUX_SOURCE
#endif
#define HAVE_SHMGET
#define HAVE_SYSLOG
#ifndef HPUX10
#define SELECT_NEEDS_CAST
typedef int rlim_t;
#endif

#elif defined(AIX)
#undef HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#define HAVE_SYS_SELECT_H
#ifndef __ps2__
#define HAVE_MMAP
#define USE_MMAP_FILES
#define HAVE_SYSLOG
#ifndef DEFAULT_GROUP
#define DEFAULT_GROUP "nobody"
#endif
#endif
#ifndef DEFAULT_USER
#define DEFAULT_USER "nobody"
#endif
#ifdef NEED_RLIM_T
typedef int rlim_t;
#endif

#elif defined(ULTRIX)
#define HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#define ULTRIX_BRAIN_DEATH
#define NEED_STRDUP
/* If you have Ultrix 4.3, and are using cc, const is broken */
#ifndef __ultrix__		/* Hack to check for pre-Ultrix 4.4 cc */
#define const			/* Not implemented */
#endif
#define HAVE_SYSLOG

#elif defined(OSF1)
#define HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#define HAVE_MMAP
#define USE_MMAP_FILES
#define HAVE_CRYPT_H
#define NO_LONG_DOUBLE
#define HAVE_SYSLOG

#elif defined(PARAGON)
#define HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#define HAVE_MMAP
#define USE_MMAP_FILES
#define HAVE_CRYPT_H
#define NO_LONG_DOUBLE
#define HAVE_SYSLOG
typedef int rlim_t;

#elif defined(SEQUENT)
#define HAVE_GMTOFF
#undef NO_KILLPG
#define NO_SETSID
#define NEED_STRDUP
#define HAVE_SYSLOG
#define tolower(c) (isupper(c) ? tolower(c) : c)

#elif defined(NEXT)
typedef unsigned short mode_t;
#define HAVE_GMTOFF
#undef NO_KILLPG
#define NO_SETSID
#define NEED_STRDUP
#define NO_LINGCLOSE
#define NO_UNISTD_H
#undef _POSIX_SOURCE
#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif
#ifndef S_ISDIR
#define S_ISDIR(m)      (((m)&(S_IFMT)) == (S_IFDIR))
#endif
#ifndef S_ISREG
#define S_ISREG(m)      (((m)&(S_IFMT)) == (S_IFREG))
#endif
#ifndef S_IXUSR
#define S_IXUSR 00100
#endif
#ifndef S_IRGRP
#define S_IRGRP 00040
#endif
#ifndef S_IXGRP
#define S_IXGRP 00010
#endif
#ifndef S_IROTH
#define S_IROTH 00004
#endif
#ifndef S_IXOTH
#define S_IXOTH 00001
#endif
#ifndef S_IRUSR
#define S_IRUSR S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR S_IWRITE
#endif
#ifndef S_IWGRP
#define S_IWGRP	000020
#endif
#ifndef S_IWOTH
#define S_IWOTH 000002
#ifndef rlim_t
typedef int rlim_t;
#endif
typedef u_long n_long;
#endif

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define waitpid(a,b,c) wait4((a) == -1 ? 0 : (a),(union wait *)(b),c,NULL)
typedef int pid_t;
#define USE_LONGJMP
#define NO_USE_SIGACTION
#define HAVE_SYSLOG

#elif defined(LINUX)
#if LINUX > 1
#include <features.h>
#if defined(__GNU_LIBRARY__) && __GNU_LIBRARY__ > 1
/* it's a glibc host */
#include <crypt.h>
#define NET_SIZE_T size_t
#endif
#define HAVE_SHMGET
#define USE_MMAP_FILES
#define HAVE_SYS_RESOURCE_H
typedef int rlim_t;
/* flock is faster ... but hasn't been tested on 1.x systems */
#define USE_FLOCK_SERIALIZED_ACCEPT
#else
#define USE_FCNTL_SERIALIZED_ACCEPT
#endif
#undef HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#undef NEED_STRDUP
#include <sys/time.h>
#define HAVE_SYSLOG

#elif defined(SCO)
#undef HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#define NEED_INITGROUPS
#define NO_WRITEV
#define SIGURG SIGUSR1		/* but note, this signal will be sent to a process group if enabled (for OOB data). It is not currently enabled. */
#include <sys/time.h>
#define HAVE_SYSLOG

#elif defined(SCO5)

#define SIGURG SIGUSR1
#define HAVE_SYS_SELECT_H
#define USE_FCNTL_SERIALIZED_ACCEPT
#define HAVE_MMAP
#define USE_MMAP_FILES
#define HAVE_SYS_RESOURCE_H
#define SecureWare
#define HAVE_SYSLOG

/* Although SCO 5 defines these in <strings.h> (note the "s") they don't have
   consts. Sigh. */
extern int strcasecmp(const char *, const char *);
extern int strncasecmp(const char *, const char *, unsigned);

#elif defined(AUX)
/* These are to let -Wall compile more cleanly */
extern int strcasecmp(const char *, const char *);
extern int strncasecmp(const char *, const char *, unsigned);
extern int set42sig(), getopt(), getpeername(), bzero();
extern int listen(), bind(), socket(), getsockname();
extern int accept(), gethostname(), connect(), lstat();
extern int select(), killpg(), shutdown();
extern int initgroups(), setsockopt();
extern char *shmat();
extern int shmctl();
extern int shmget();
extern char *sbrk();
extern char *crypt();
#include <sys/time.h>
#undef HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#define NEED_STRDUP
/* fcntl() locking is expensive with NFS */
#define USE_FLOCK_SERIALIZED_ACCEPT
#define HAVE_SHMGET
/*
 * NOTE: If when you run Apache under A/UX and you get a warning
 * that httpd couldn't move break, then the below value for
 * MOVEBREAK (64megs) is too large for your setup. Try reducing
 * to 0x2000000 which is still PLENTY of space. I doubt if
 * even on heavy systems sbrk() would be called at all...
 */
#define MOVEBREAK		0x4000000
#define NO_LINGCLOSE
#define NO_SLACK
#define HAVE_SYSLOG

#elif defined(SVR4)
#define NO_KILLPG
#undef  NO_SETSID
#undef NEED_STRDUP
#define NEED_STRCASECMP
#ifndef ENCORE
#define NEED_STRNCASECMP
#endif
#define bzero(a,b) memset(a,0,b)
/* A lot of SVR4 systems need this */
#define USE_FCNTL_SERIALIZED_ACCEPT
#define HAVE_SYSLOG
#ifdef SNI  /* SINIX/ReliantUNIX, probably other SVR4's as well */
#define NET_SIZE_T size_t
#endif /*SNI*/

#elif defined(UW)
#define NO_LINGCLOSE
#define NO_KILLPG
#undef  NO_SETSID
#undef NEED_STRDUP
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#define bzero(a,b) memset(a,0,b)
#define HAVE_RESOURCE
#define HAVE_MMAP
#define USE_MMAP_FILES
#define HAVE_SHMGET
#define HAVE_CRYPT_H
#define HAVE_SYS_SELECT_H
#define HAVE_SYS_RESOURCE_H
#include <sys/time.h>
#define _POSIX_SOURCE
#define NET_SIZE_T size_t
#define HAVE_SYSLOG

#elif defined(DGUX)
#define NO_KILLPG
#undef  NO_SETSID
#undef NEED_STRDUP
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#define bzero(a,b) memset(a,0,b)
/* A lot of SVR4 systems need this */
#define USE_FCNTL_SERIALIZED_ACCEPT
#define ap_inet_addr inet_network
#define HAVE_SYSLOG

#elif defined(__NetBSD__) || defined(__OpenBSD__)
#define HAVE_SYS_RESOURCE_H
#define HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#define HAVE_SYSLOG
#ifndef DEFAULT_USER
#define DEFAULT_USER "nobody"
#endif
#ifndef DEFAULT_GROUP
#define DEFAULT_GROUP "nogroup"
#endif

#elif defined(UTS21)
#undef HAVE_GMTOFF
#undef NO_KILLPG
#define NO_SETSID
#define NEED_WAITPID
#define NO_OTHER_CHILD
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define HAVE_SYSLOG
#define strftime(buf,bufsize,fmt,tm)    ascftime(buf,fmt,tm)
#include <sys/types.h>

#elif defined(APOLLO)
#undef HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#define HAVE_SYSLOG

#elif defined(__FreeBSD__) || defined(__bsdi__)
#if defined(__FreeBSD__)
#include <osreldate.h>
#endif
#define HAVE_SYS_RESOURCE_H
#define HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#define HAVE_MMAP
#define USE_MMAP_FILES
#ifndef DEFAULT_USER
#define DEFAULT_USER "nobody"
#endif
#ifndef DEFAULT_GROUP
#define DEFAULT_GROUP "nogroup"
#endif
#if defined(__bsdi__) || \
(defined(__FreeBSD_version) && (__FreeBSD_version < 220000))
typedef quad_t rlim_t;
#endif
#define USE_FLOCK_SERIALIZED_ACCEPT
#define HAVE_SYSLOG

#elif defined(QNX)
#ifndef crypt
char *crypt(const char *pw, const char *salt);
#endif
#ifndef initgroups
int initgroups(char *, int);
#endif
#ifndef strncasecmp
#define strncasecmp strnicmp
#endif
#undef NO_KILLPG
#undef NO_SETSID
#define NEED_INITGROUPS
#define NEED_SELECT_H
#define NEED_PROCESS_H
#define HAVE_SYS_SELECT_H
#include <unix.h>
#define HAVE_MMAP
#define HAVE_SYSLOG

#elif defined(LYNXOS)
#undef NO_KILLPG
#undef NO_SETSID
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#define NEED_INITGROUPS
#define HAVE_SYSLOG

#elif defined(UXPDS)
#undef NEED_STRCASECMP
#undef NEED_STRNCASECMP
#undef NEED_STRDUP
#undef HAVE_GMTOFF
#define NO_KILLPG
#undef NO_SETSID
#define HAVE_RESOURCE 1
#define bzero(a,b) memset(a,0,b)
#define USE_FCNTL_SERIALIZED_ACCEPT
#define HAVE_MMAP
#define USE_MMAP_FILES
#define HAVE_CRYPT_H
#define HAVE_SYSLOG

#elif defined(__EMX__)
/* Defines required for EMX OS/2 port. */
#define NO_KILLPG
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#define NO_SETSID
#define NO_TIMES
/* Add some drive name support */
#define chdir _chdir2
#include <sys/time.h>
#define MAXSOCKETS 4096
#define HAVE_MMAP
#define NO_RELIABLE_PIPED_LOGS

#elif defined(__MACHTEN__)
typedef int rlim_t;
#undef NO_KILLPG
#define NO_SETSID
#define HAVE_GMTOFF
#ifndef __MACHTEN_PPC__
#ifndef __MACHTEN_68K__
#define __MACHTEN_68K__
#endif
#define USE_FLOCK_SERIALIZED_ACCEPT
#define NO_USE_SIGACTION
#define JMP_BUF sigjmp_buf
#define USE_LONGJMP
#undef NEED_STRDUP
#else
#define HAVE_SHMGET
#define USE_FCNTL_SERIALIZED_ACCEPT
#endif

/* Convex OS v11 */
#elif defined(CONVEXOS11)
#undef HAVE_GMTOFF
#undef NO_KILLPG
#undef NO_SETSID
#undef NEED_STRDUP
#define HAVE_MMAP
#define USE_MMAP_FILES
#define HAVE_SYSLOG

#define NO_TIMEZONE
#include <stdio.h>
#include <sys/types.h>
typedef int rlim_t;

#elif defined(ISC)
#include <net/errno.h>
#define NO_KILLPG
#undef NO_SETSID
#define HAVE_SHMGET
#define SIGURG SIGUSR1
#define USE_FCNTL_SERIALIZED_ACCEPT
#define HAVE_SYSLOG

#elif defined(NEWSOS)
#define HAVE_SYS_RESOURCE_H
#define HAVE_SHMGET
#define USE_LONGJMP
#define NO_SETSID
#define NO_USE_SIGACTION
#define NEED_WAITPID
#define NO_OTHER_CHILD
#define HAVE_SYSLOG
#include <sys/time.h>
#include <stdlib.h>
#include <sys/types.h>
typedef int pid_t;
typedef int rlim_t;
typedef int mode_t;

#elif defined(RISCIX)
#include <sys/time.h>
typedef int rlim_t;
#define NO_USE_SIGACTION
#define USE_LONGJMP
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#define NEED_STRDUP

#elif defined(BEOS)
#include <stddef.h>

#define NO_WRITEV
#define NO_KILLPG
#define NEED_INITGROUPS

/* BeOS doesn't have a couple signals... redefine to close ones */
#define SIGBUS SIGSEGV
#define SIGURG SIGPIPE

#define isascii(c)	(!((c) & ~0177))

#elif defined(WIN32)

/* All windows stuff is now in os/win32/os.h */

#else
/* Unknown system - Edit these to match */
#ifdef BSD
#define HAVE_GMTOFF
#else
#undef HAVE_GMTOFF
#endif
/* NO_KILLPG is set on systems that don't have killpg */
#undef NO_KILLPG
/* NO_SETSID is set on systems that don't have setsid */
#undef NO_SETSID
/* NEED_STRDUP is set on stupid systems that don't have strdup. */
#undef NEED_STRDUP
#endif

#ifndef API_EXPORT
#define API_EXPORT(type)    type
#endif

#ifndef API_EXPORT_NONSTD
#define API_EXPORT_NONSTD(type)    type
#endif

#ifndef MODULE_VAR_EXPORT
#define MODULE_VAR_EXPORT
#endif
#ifndef API_VAR_EXPORT
#define API_VAR_EXPORT
#endif

/* So that we can use inline on some critical functions, and use
 * GNUC attributes (such as to get -Wall warnings for printf-like
 * functions).  Only do this in gcc 2.7 or later ... it may work
 * on earlier stuff, but why chance it.
 */
#if !defined(__GNUC__) || __GNUC__ < 2 || __GNUC_MINOR__ < 7
#define ap_inline
#define __attribute__(__x)
#else
#define ap_inline __inline__
#define USE_GNU_INLINE
#endif

/* Do we have sys/resource.h; assume that BSD does. */
#ifndef HAVE_SYS_RESOURCE_H
#ifdef BSD
#define HAVE_SYS_RESOURCE_H
#endif
#endif /* HAVE_SYS_RESOURCE_H */

/*
 * The particular directory style your system supports. If you have dirent.h
 * in /usr/include (POSIX) or /usr/include/sys (SYSV), #include 
 * that file and define DIR_TYPE to be dirent. Otherwise, if you have 
 * /usr/include/sys/dir.h, define DIR_TYPE to be direct and include that
 * file. If you have neither, I'm confused.
 */

#include <sys/types.h>
#include <stdarg.h>
/*
 * We use snprintf() to avoid overflows, but we include
 * our own version (ap_snprintf). Allow for people to use their
 * snprintf() if they want
 */
#ifdef HAVE_SNPRINTF
#define ap_snprintf     snprintf
#define ap_vsnprintf    vsnprintf
#else
API_EXPORT(int) ap_snprintf(char *buf, size_t len, const char *format,...)
			    __attribute__((format(printf,3,4)));
API_EXPORT(int) ap_vsnprintf(char *buf, size_t len, const char *format,
			     va_list ap);
#endif

#if !defined(NEXT) && !defined(WIN32)
#include <dirent.h>
#define DIR_TYPE dirent
#elif !defined(WIN32)
#include <sys/dir.h>
#define DIR_TYPE direct
#else
#define DIR_TYPE dirent
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#if !defined(MPE) && !defined(WIN32)
#include <sys/file.h>
#endif
#ifndef WIN32
#include <sys/socket.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#if !defined(MPE) && !defined(BEOS)
#include <arpa/inet.h>		/* for inet_ntoa */
#endif
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <limits.h>
#define closesocket(s) close(s)
#ifndef O_BINARY
#define O_BINARY (0)
#endif

#else /* WIN32 */
#include <winsock.h>
#include <malloc.h>
#include <io.h>
#include <fcntl.h>
#endif /* ndef WIN32 */

#include <time.h>		/* for ctime */
#include <signal.h>
#include <errno.h>
#if !defined(QNX) && !defined(CONVEXOS11) && !defined(NEXT)
#include <memory.h>
#endif


#ifdef NEED_PROCESS_H
#include <process.h>
#endif

#include <regex.h>

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#ifdef SUNOS4
int getrlimit(int, struct rlimit *);
int setrlimit(int, struct rlimit *);
#endif
#endif
#ifdef HAVE_MMAP
#if !defined(__EMX__) && !defined(WIN32)
/* This file is not needed for OS/2 */
#include <sys/mman.h>
#endif
#endif
#if !defined(MAP_ANON) && defined(MAP_ANONYMOUS)
#define MAP_ANON MAP_ANONYMOUS
#endif

#if defined(HAVE_MMAP) && defined(NO_MMAP)
#undef HAVE_MMAP
#endif

#ifndef LOGNAME_MAX
#define LOGNAME_MAX 25
#endif

#if !defined(NEXT) && !defined(WIN32)
#include <unistd.h>
#endif

#ifdef ultrix
#define ULTRIX_BRAIN_DEATH
#endif

#ifndef S_ISLNK
#ifndef __EMX__
/* Don't define this for OS/2 */
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned long) -1)
#endif

/*
 * Replace signal function with sigaction equivalent
 */
#ifndef NO_USE_SIGACTION
typedef void Sigfunc(int);

#if defined(SIG_IGN) && !defined(SIG_ERR)
#define SIG_ERR ((Sigfunc *)-1)
#endif

/*
 * For some strange reason, QNX defines signal to signal. Eliminate it.
 */
#ifdef signal
#undef signal
#endif
#define signal(s,f)	ap_signal(s,f)
Sigfunc *signal(int signo, Sigfunc * func);
#endif

#include <setjmp.h>

#if defined(USE_LONGJMP)
#define ap_longjmp(x, y)        longjmp((x), (y))
#define ap_setjmp(x)            setjmp(x)
#ifndef JMP_BUF
#define JMP_BUF jmp_buf
#endif
#else
#define ap_longjmp(x, y)        siglongjmp((x), (y))
#define ap_setjmp(x)            sigsetjmp((x), 1)
#ifndef JMP_BUF
#define JMP_BUF sigjmp_buf
#endif
#endif

#ifdef SELECT_NEEDS_CAST
#define ap_select(_a, _b, _c, _d, _e)	\
    select((_a), (int *)(_b), (int *)(_c), (int *)(_d), (_e))
#else
#define ap_select	select
#endif

#ifndef ap_inet_addr
#define ap_inet_addr inet_addr
#endif

#ifdef NO_OTHER_CHILD
#define NO_RELIABLE_PIPED_LOGS
#endif

/* Finding offsets of elements within structures.
 * Taken from the X code... they've sweated portability of this stuff
 * so we don't have to.  Sigh...
 */

#if defined(CRAY) || defined(__arm)
#ifdef __STDC__
#define XtOffset(p_type,field) _Offsetof(p_type,field)
#else
#ifdef CRAY2
#define XtOffset(p_type,field) \
	(sizeof(int)*((unsigned int)&(((p_type)NULL)->field)))

#else /* !CRAY2 */

#define XtOffset(p_type,field) ((unsigned int)&(((p_type)NULL)->field))

#endif /* !CRAY2 */
#endif /* __STDC__ */
#else /* ! (CRAY || __arm) */

#define XtOffset(p_type,field) \
	((long) (((char *) (&(((p_type)NULL)->field))) - ((char *) NULL)))

#endif /* !CRAY */

#ifdef offsetof
#define XtOffsetOf(s_type,field) offsetof(s_type,field)
#else
#define XtOffsetOf(s_type,field) XtOffset(s_type*,field)
#endif

/* some architectures require size_t * pointers where others require int *
 * pointers in functions such as accept(), getsockname(), getpeername()
 */
#ifndef NET_SIZE_T
#define NET_SIZE_T int
#endif

#ifdef SUNOS_LIB_PROTOTYPES
/* Prototypes needed to get a clean compile with gcc -Wall.
 * Believe it or not, these do have to be declared, at least on SunOS,
 * because they aren't mentioned in the relevant system headers.
 * Sun Quality Software.  Gotta love it.
 */

int getopt(int, char **, char *);

int strcasecmp(char *, char *);
int strncasecmp(char *, char *, int);
int toupper(int);
int tolower(int);

int printf(char *,...);
int fprintf(FILE *, char *,...);
int fputs(char *, FILE *);
int fread(char *, int, int, FILE *);
int fwrite(char *, int, int, FILE *);
int fflush(FILE *);
int fclose(FILE *);
int ungetc(int, FILE *);
int _filbuf(FILE *);	/* !!! */
int _flsbuf(unsigned char, FILE *);	/* !!! */
int sscanf(char *, char *,...);
void setbuf(FILE *, char *);
void perror(char *);

time_t time(time_t *);
int strftime(char *, int, char *, struct tm *);

int initgroups(char *, int);
int wait3(int *, int, void *);	/* Close enough for us... */
int lstat(const char *, struct stat *);
int stat(const char *, struct stat *);
int flock(int, int);
#ifndef NO_KILLPG
int killpg(int, int);
#endif
int socket(int, int, int);
int setsockopt(int, int, int, const char *, int);
int listen(int, int);
int bind(int, struct sockaddr *, int);
int connect(int, struct sockaddr *, int);
int accept(int, struct sockaddr *, int *);
int shutdown(int, int);

int getsockname(int s, struct sockaddr *name, int *namelen);
int getpeername(int s, struct sockaddr *name, int *namelen);
int gethostname(char *name, int namelen);
void syslog(int, char *,...);
char *mktemp(char *);

long vfprintf(FILE *, char *, va_list);

#endif /* SUNOS_LIB_PROTOTYPES */

/* The assumption is that when the functions are missing,
 * then there's no matching prototype available either.
 * Declare what is needed exactly as the replacement routines implement it.
 */
#ifdef NEED_STRDUP
extern char *strdup (const char *str);
#endif
#ifdef NEED_STRCASECMP
extern int strcasecmp (const char *a, const char *b);
#endif
#ifdef NEED_STRNCASECMP
extern int strncasecmp (const char *a, const char *b, int n);
#endif
#ifdef NEED_INITGROUPS
extern int initgroups(const char *name, gid_t basegid);
#endif
#ifdef NEED_WAITPID
extern int waitpid(pid_t pid, int *statusp, int options);
#endif
#ifdef NEED_STRERROR
extern char *strerror (int err);
#endif
#ifdef NEED_DIFFTIME
extern double difftime(time_t time1, time_t time0);
#endif