/* s5066d.c  -  NATO STANAG 5066 Annex A, B, and C daemon
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * This is confidential unpublished proprietary source code of the author.
 * NO WARRANTY, not even implied warranties. Contains trade secrets.
 * Distribution prohibited unless authorized in writing. See file COPYING.
 * $Id: s5066d.c,v 1.12 2006/06/18 00:44:51 sampo Exp $
 *
 * 15.4.2006, started work over Easter holiday --Sampo
 * 22.4.2006, added more options over the weekend --Sampo
 *
 * This file contains option processing, configuration, and main().
 */

#include <signal.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#ifdef HAVE_NET_SNMP
#include "snmpInterface.h"
#endif

/*#include "dialout.h"       / * Async serial support */
#include "serial_sync.h"   /* Sync serial support */
#include "errmac.h"
#include "hiios.h"
#include "afr.h"
#include "s5066.h"

int read_all_fd(int fd, char* p, int want, int* got_all);
int write_all_fd(int fd, char* p, int pending);
int write_or_append_lock_c_path(char* c_path, char* data, int len, CU8* lk, int seeky, int flag);

CU8* help =
"s5066d  -  NATO STANAG 5066 Annex A, B, and C daemon - R" REL "\n\
STANAG 5066 is a standard for High Frequency radio communications.\n\
Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.\n\
NO WARRANTY, not even implied warranties.\n\
This product includes software developed by the NATO C3 Agency The Hague\n\
the Netherlands and its contributors. Run `s5066d -license' for details.\n\
Send well researched bug reports to the author. Home: open5066.org\n\
\n\
Usage: s5066d [options] PROTO:REMOTEHOST:PORT\n\
       echo secret | s5066d -p sis::5066 -c AES256 -k 0 dts:quebec.cellmail.com:5067\n\
       echo secret | s5066d -p sis::5066 -c AES256 -k 0 dts:/dev/se_hdlc1:S-9600-1000-8N1\n\
       s5066d -p smtp::25 sis:localhost:5066 smtp:mail.cellmail.com:25\n\
  -p  PROT:IF:PORT Protocol, network interface and TCP port for listening\n\
                   connections. If you omit interface, all interfaces are bound.\n\
                     sis:0.0.0.0:5066   - Listen for SIS (Annex A primitives)\n\
                     dts:0.0.0.0:5067   - Listen for DTS (Annex B)\n\
                     dts:/dev/TTY:CONF  - Listen for DTS using serial device\n\
                                        - where CONF is S-B-F-CPS and \n\
                                        - S := {S|A} (sync or async) \n\
                                        - B := integer (baud) \n\
                                        - F := integer (frame size) \n\
                                        - C := {6|7|8} (number of bits per character) \n\
                                        - P := {E|O|N} (parity: even, odd and none) \n\
                                        - S := {1|2} (stop bits: 1 or 2) \n\
                                        - The only tested CONF is S-115200-1000-8N1 \n\
                     smtp:0.0.0.0:25    - Listen for SMTP (RFC 2821)\n\
                     http:0.0.0.0:80    - Listen for HTTP/1.0 (simplified)\n\
                     tp:0.0.0.0:5068    - Listen for test ping protocol\n\
  -t  SECONDS      Connection timeout for both SIS and DTS. Default: 0=no timeout.\n\
  -c  CIPHER       Enable crypto on DTS interface using specified cipher. Use '?' for list.\n\
  -k  FDNUMBER     File descriptor for reading symmetric key. Use 0 for stdin.\n\
  -nfd  NUMBER     Maximum number of file descriptors, i.e. simultaneous\n\
                   connections. Default 20 (about 16 connections).\n\
  -npdu NUMBER     Maximum number of simultaneously active PDUs. Default 60.\n\
  -nthr NUMBER     Number of threads. Default 1. Should not exceed number of CPUs.\n\
  -nkbuf BYTES     Size of kernel buffers. Default is not to change kernel buffer size.\n\
  -nlisten NUMBER  Listen backlog size. Default 128.\n\
  -egd PATH        Specify path of Entropy Gathering Daemon socket, default on\n\
                   Solaris: /tmp/entropy. On Linux /dev/urandom is used instead\n\
                   See http://www.lothar.com/tech/crypto/ or\n\
                   http://www.aet.tu-cottbus.de/personen/jaenicke/postfix_tls/prngd.html\n\
  -rand PATH       Location of random number seed file. On Solaris EGD is used.\n\
                   On Linux the default is /dev/urandom. See RFC1750.\n\
  -snmp PORT       Enable SNMP agent (if compiled with Net SNMP).\n\
  -uid UID:GID     If run as root, drop privileges and assume specified uid and gid.\n\
  -pid PATH        Write process id in the supplied path\n\
  -watchdog        Enable built-in watch dog\n\
  -kidpid PATH     Write process id of the child of watchdog in the supplied path\n\
  -afr size_MB     Turn on Application Flight Recorder. size_MB is per thread buffer.\n\
  -v               Verbose messages.\n\
  -q               Be extra quiet.\n\
  -d               Turn on debugging.\n\
  -license         Show licensing details, including NATO C3 Agency disclaimer.\n\
  -h               This help message\n\
  --               End of options\n\
N.B. Although s5066d is a 'daemon', it does not daemonize itself. You can always say s5066d&\n";

char* instance = "s5066d";  /* how this server is identified in logs */
int afr_buf_size = 0;
int verbose = 1;
int debug = 0;
int debugpoll = 0;
int timeout = 0;
int nfd = 20;
int npdu = 60;
int nthr = 1;
int nkbuf = 0;
int listen_backlog = 128;   /* what is right tuning for this? */
int gcthreshold = 0;
int leak_free = 0;
int assert_nonfatal = 0;
int drop_uid = 0;
int drop_gid = 0;
int watchdog;
int snmp_port = 0;
char* pid_path;
char* kidpid_path;
char* rand_path;
char* egd_path;
char  symmetric_key[1024];
int symmetric_key_len;
struct hi_host_spec* listen_ports = 0;
struct hi_host_spec* remotes = 0;

struct hi_proto prototab[] = {
  { "dummy0",  0, 0 },
  { "sis",  5066, 0 },
  { "dts",  5067, 0 },
  { "smtp",   25, 0 },
  { "http", 8080, 0 },
  { "tp",   5068, 0 },
  { "", 0 }
};

char remote_station_addr[] = { 0x61, 0x89, 0x00, 0x00 };   /* *** temp kludge */
struct hiios* shuff;        /* Main I/O shuffler object */

#define SNMPLOGFILE "/var/tmp/snmpOpen5066.log"

/* proto:host:port or proto:host or proto::port */

int parse_port_spec(char* arg, struct hi_host_spec** head, char* default_host)
{
  struct hostent* he;
  char prot[8];
  char host[256];
  int proto, port, ret;
  struct hi_host_spec* hs;
  
  ret = sscanf(arg, "%8[^:]:%255[^:]:%i", prot, host, &port);
  switch (ret) {
  case 2:
    port = -1;   /* default */
  case 3:
    if (!strlen(prot)) {
      ERR("Bad proto:host:port spec(%s). You MUST specify proto.", arg);
      exit(5);
    }
    for (proto = 0; prototab[proto].name[0]; ++proto)
      if (!strcmp(prototab[proto].name, prot))
	break;
    if (!prototab[proto].name[0]) {
      ERR("Bad proto:host:port spec(%s). Unknown proto.", arg);
      exit(5);
    }
    if (port == -1)
      port = prototab[proto].default_port;
    if (strlen(host))
      default_host = host;
    break;
  default:
    ERR("Bad proto:host:port spec(%s). %d", arg, ret);
    return 0;
  }
  
  D("arg(%s) parsed as proto(%s)=%d host(%s) port(%d)", arg, prot, proto, host, port);
  ZMALLOC(hs);
  
  if (default_host[0] == '/') {  /* Its a serial port */
    hs->sin.sin_family = 0xfead;
  } else {
    he = gethostbyname(default_host);
    if (!he) {
      ERR("hostname(%s) did not resolve(%d)", default_host, h_errno);
      exit(5);
    }
    
    hs->sin.sin_family = AF_INET;
    hs->sin.sin_port = htons(port);
    memcpy(&(hs->sin.sin_addr.s_addr), he->h_addr, sizeof(hs->sin.sin_addr.s_addr));
  }
  hs->specstr = arg;
  hs->proto = proto;
  hs->next = *head;
  *head = hs;
  return 1;
}

void opt(int* argc, char*** argv, char*** env)
{
  if (*argc <= 1) goto argerr;
  
  while (1) {
    ++(*argv); --(*argc);
    
    if (!(*argc) || ((*argv)[0][0] != '-')) break;  /* probably the remote host and port */
    
    switch ((*argv)[0][1]) {
    case '-': if ((*argv)[0][2]) break;
      ++(*argv); --(*argc);
      DD("End of options by --");
      return;  /* -- ends the options */

    case 'a': if ((*argv)[0][2] != 'f' || (*argv)[0][3] != 'r' || (*argv)[0][4]) break;
      ++(*argv); --(*argc);
      if (!(*argc)) break;
      afr_buf_size = atoi((*argv)[0]);
      afr_buf_size = afr_buf_size << 20;  /* Mega bytes */
      if (afr_buf_size)
	afr_add_thread(afr_buf_size,1);
      continue;

    case 'n':
      switch ((*argv)[0][2]) {
      case 'f': if ((*argv)[0][3] != 'd' || (*argv)[0][4]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	nfd = atoi((*argv)[0]);
	continue;
      case 'p': if ((*argv)[0][3] != 'd' || (*argv)[0][4] != 'u' || (*argv)[0][5]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	npdu = atoi((*argv)[0]);
	continue;
      case 't': if ((*argv)[0][3] != 'h' || (*argv)[0][4] != 'r' || (*argv)[0][5]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	nthr = atoi((*argv)[0]);
	continue;
      case 'k': if ((*argv)[0][3] != 'b' || (*argv)[0][4] != 'u' || (*argv)[0][5] != 'f' || (*argv)[0][6]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	nkbuf = atoi((*argv)[0]);
	continue;
      case 'l': if ((*argv)[0][3] != 'i' || (*argv)[0][4] != 's' || (*argv)[0][5]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	listen_backlog = atoi((*argv)[0]);
	continue;
      }
      break;

    case 's':
      switch ((*argv)[0][2]) {
      case 'n': if ((*argv)[0][3] != 'm' || (*argv)[0][4] != 'p' || (*argv)[0][5]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	snmp_port = atoi((*argv)[0]);
	continue;
      }
      break;

    case 't': if ((*argv)[0][2]) break;
      ++(*argv); --(*argc);
      if (!(*argc)) break;
      timeout = atoi((*argv)[0]);
      continue;

    case 'd':
      switch ((*argv)[0][2]) {
      case '\0':
	++debug;
	continue;
      case 'p':  if ((*argv)[0][3]) break;
	++debugpoll;
	continue;
      case 'i':  if ((*argv)[0][3]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	instance = (*argv)[0];
	continue;
      }
      break;

    case 'v':
      switch ((*argv)[0][2]) {
      case '\0':
	++verbose;
	continue;
      }
      break;

    case 'q':
      switch ((*argv)[0][2]) {
      case '\0':
	verbose = 0;
	continue;
      }
      break;

    case 'e':
      switch ((*argv)[0][2]) {
      case 'g': if ((*argv)[0][3] != 'd' || (*argv)[0][4]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	egd_path = (*argv)[0];
	continue;
      }
      break;
      
    case 'r':
      switch ((*argv)[0][2]) {
      case 'f':
	AFR_TS(LEAK, 0, "memory leaks enabled");
#if 1
	ERR("*** WARNING: You have turned memory frees to memory leaks. We will (eventually) run out of memory. Using -rf is not recommended. %d\n", 0);
#endif
	++leak_free;
	continue;
#if 0
      case 'e':
	if ((*argv)[0][3]) break;
	++(*argv); --(*argc);
	if ((*argc) < 4) break;
	sscanf((*argv)[0], "%i", &abort_funcno);
	++(*argv); --(*argc);
	sscanf((*argv)[0], "%i", &abort_line);
	++(*argv); --(*argc);
	sscanf((*argv)[0], "%i", &abort_error_code);
	++(*argv); --(*argc);
	sscanf((*argv)[0], "%i", &abort_iter);
	fprintf(stderr, "Will force core upon %x:%x err=%d iter=%d\n",
		abort_funcno, abort_line, abort_error_code, abort_iter);
	continue;
#endif
      case 'g':
	if ((*argv)[0][3]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	gcthreshold = atoi((*argv)[0]);
	if (!gcthreshold)
	  ERR("*** WARNING: You have disabled garbage collection. This may lead to increased memory consumption for scripts that handle a lot of PDUs or run for long time. Using `-rg 0' is not recommended. %d\n", 0);
	continue;
      case 'a':
	if ((*argv)[0][3] == 0) {
	  AFR_TS(ASSERT_NONFATAL, 0, "assert nonfatal enabled");
#if 1
	  ERR("*** WARNING: YOU HAVE TURNED ASSERTS OFF USING -ra FLAG. THIS MEANS THAT YOU WILL NOT BE ABLE TO OBTAIN ANY SUPPORT. IF PROGRAM NOW TRIES TO ASSERT IT MAY MYSTERIOUSLY AND UNPREDICTABLY CRASH INSTEAD, AND NOBODY WILL BE ABLE TO FIGURE OUT WHAT WENT WRONG OR HOW MUCH DAMAGE MAY BE DONE. USING -ra IS NOT RECOMMENDED. %d\n", assert_nonfatal);
#endif
	  ++assert_nonfatal;
	  continue;
	}
	if (!strcmp((*argv)[0],"-rand")) {
	  ++(*argv); --(*argc);
	  if (!(*argc)) break;
	  rand_path = (*argv)[0];
	  continue;
	}
	break;
      }
      break;

    case 'w':
      switch ((*argv)[0][2]) {
      case 'a':
	if (!strcmp((*argv)[0],"-watchdog")) {
	  ++watchdog;
	  continue;
	}
	break;
      }
      break;

    case 'p':
      switch ((*argv)[0][2]) {
      case '\0':
	++(*argv); --(*argc);
	if (!(*argc)) break;
	if (!parse_port_spec((*argv)[0], &listen_ports, "0.0.0.0")) break;
	continue;
      case 'i':
	if (!strcmp((*argv)[0],"-pid")) {
	  ++(*argv); --(*argc);
	  if (!(*argc)) break;
	  pid_path = (*argv)[0];
	  continue;
	}
	break;
      }
      break;

    case 'k':
      switch ((*argv)[0][2]) {
      case 'i':
	if (!strcmp((*argv)[0],"-kidpid")) {
	  ++(*argv); --(*argc);
	  if (!(*argc)) break;
	  kidpid_path = (*argv)[0];
	  continue;
	}
	break;
      case '\0':
	++(*argv); --(*argc);
	if (!(*argc)) break;
	read_all_fd(atoi((*argv)[0]), symmetric_key, sizeof(symmetric_key), &symmetric_key_len);
	D("Got %d characters of symmetric key", symmetric_key_len);
	continue;
      }
      break;

    case 'c': if ((*argv)[0][2]) break;
      ++(*argv); --(*argc);
      if (!(*argc)) break;
#ifndef ENCRYPTION
      ERR("Encryption not compiled in. %d",0);
#endif
      continue;

    case 'u':
      switch ((*argv)[0][2]) {
      case 'i': if ((*argv)[0][3] != 'd' || (*argv)[0][4]) break;
	++(*argv); --(*argc);
	if (!(*argc)) break;
	sscanf((*argv)[0], "%i:%i", &drop_uid, &drop_gid);
	continue;
      }
      break;

    case 'l':
      switch ((*argv)[0][2]) {
      case 'i':
	if (!strcmp((*argv)[0],"-license")) {
	  extern char* license;
	  fprintf(stderr, license);
	  exit(0);
	}
	break;
      }
      break;

    } 
    /* fall thru means unrecognized flag */
    if (*argc)
      fprintf(stderr, "Unrecognized flag `%s'\n", (*argv)[0]);
  argerr:
    fprintf(stderr, help);
    exit(3);
  }
  
  /* Remaining commandline is the remote host spec for DTS */
  while (*argc) {
    if (!parse_port_spec((*argv)[0], &remotes, "127.0.0.1")) break;
    ++(*argv); --(*argc);
  }
  
  if (nfd < 1)  nfd = 1;
  if (npdu < 1) npdu = 1;
  if (nthr < 1) nthr = 1;
}

/* Parse serial port config string and do all the ioctls to get it right. */

static struct hi_io* serial_init(struct hi_host_spec* hs)
{
  char tty[256];
  char sync = 'S', parity = 'N';
  int fd, ret, baud = 9600, bits = 8, stop = 1, framesize = 1000;
  ret = sscanf(hs->specstr, "dts:%255[^:]:%c-%d-%d-%d%c%d",
	       tty, &sync, &baud, &framesize, &bits, &parity, &stop);
  if (ret < 4) {
    fprintf(stderr, "You must supply serial port name and config, e.g. `dts:/dev/ttyS0:A-9600-8N1'. You gave(%s). You loose.\n", hs->specstr);
    exit(3);
  }
  fd = open(tty, O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd == -1) {
    ERR("open(%s): Error opening serial port: %d %s", tty, errno, STRERROR(errno));
    exit(3);
  }
  if (verbose)
    log_port_info(fd, tty, "before");
  if (set_baud_rate(fd, tty, baud) == -1)
    exit(3);
  if (set_frame_size(fd, tty, framesize) == -1)
    exit(3);
  if (verbose)
    log_port_info(fd, tty, "after");
  nonblock(fd);
  return hi_add_fd(shuff, fd, hs->proto, HI_TCP_C, hs->specstr);
}

void* thread_loop(void* _shf)
{
  struct hi_thr hit;
  struct hiios* shf = (struct hiios*)_shf;
  memset(&hit, 0, sizeof(hit));
  if (afr_buf_size)
    afr_add_thread(afr_buf_size, 1);
  hi_shuffle(&hit, shf);
  return 0;
}

/* ============== M A I N ============== */

int main(int argc, char** argv, char** env)
{ 
  struct hi_thr hit;
  memset(&hit, 0, sizeof(hit));
  afr_init(*argv);
#ifdef MINGW
  pthread_mutex_init(&dsdbilock, 0);
  pthread_mutex_init(&shuff_mutex, 0);
  pthread_mutex_init(&gethostbyname_mutex, 0);
  {
    WSADATA wsaDat;
    WORD vers = MAKEWORD(2,2);  /* or 2.0? */
    ret = WSAStartup(vers, &wsaDat);
    if (ret) {
      DSERR_DETECT(EBM_PROTO_TCP, ret, "WinSock DLL could not be initialized: %d", ret);
      return -1;
    }
  }
#endif
#if !defined(MACOS) && !defined(MINGW)
# ifdef MUTEX_DEBUG
  if (pthread_mutexattr_init(MUTEXATTR)) NEVERNEVER("unable to initialize mutexattr %d",argc);
  if (pthread_mutexattr_settype(MUTEXATTR, PTHREAD_MUTEX_ERRORCHECK_NP))
    DSNEVERNEVER("unable to set mutexattr %d",argc);
# endif
#endif
#ifdef COMPILED_DATE
  int now = time(0);
  if (COMPILED_DATE + TWO_MONTHS < now) {   /* *** this logic needs refinement and error code of its own --Sampo */
     if (COMPILED_DATE+ THREE_MONTHS < now){ 
        DSCRIT_DETECT(ECONF_DEMO_EXP,0,"Evaluation copy expired.");
	exit(4);
     } else
        DSCRIT_DETECT(ECONF_DEMO_EXP,0,"Evaluation copy expired, in %d secs this program will stop working", COMPILED_DATE + THREE_MONTHS-now);
  } else {
    if (now + ONE_DAY < COMPILED_DATE){
      DSCRIT_DETECT(ECONF_DEMO_EXP,0,"Check for demo erroneus");
      exit(4);
    }
  }
#endif
  
  /*openlog("s5066d", LOG_PID, LOG_LOCAL0);     Do we want syslog logging? */
  opt(&argc, &argv, &env);

  /*if (stats_prefix) init_cmdline(argc, argv, env, stats_prefix);*/
  CMDLINE("init");
  
  if (pid_path) {
    int len;
    char buf[INTSTRLEN];
    len = sprintf(buf, "%d", (int)getpid());
    DD("pid_path=`%s'", pid_path);
    if (write_or_append_lock_c_path(pid_path, buf, len, "write pid", SEEK_SET, O_TRUNC) <= 0) {
      ERR("Failed to write PID file at `%s'. Check that all directories exist and that permissions allow dsproxy (pid=%d, euid=%d, egid=%d) to write the file. Disk could also be full or ulimit(1) too low. Continuing anyway.",
	  pid_path, getpid(), geteuid(), getegid());
    }
  }
  
  if (watchdog) {
#ifdef MINGW
    ERR("Watch dog feature not supported on Windows.");
#else
    int ret, watch_dog_iteration = 0;
    while (1) {
      ++watch_dog_iteration;
      D("Watch dog loop %d", watch_dog_iteration);
      switch (ret = fork()) {
      case -1:
	ERR("Watch dog %d: attempt to fork() real server failed: %d %s. Perhaps max number of processes has been reached or we are out of memory. Will try again in a sec. To stop a vicious cycle `kill -9 %d' to terminate this watch dog.",
	    watch_dog_iteration, errno, STRERROR(errno), getpid());
	break;
      case 0:   goto normal_child;  /* Only way out of this loop */
      default:
	/* Reap the child */
	switch (waitpid(ret, &ret, 0)) {
	case -1:
	  ERR("Watch dog %d: attempt to waitpid() real server failed: %d %s. To stop a vicious cycle `kill -9 %d' to terminate this watch dog.",
			watch_dog_iteration, errno, STRERROR(errno), getpid());
	  break;
	default:
	  ERR("Watch dog %d: Real server exited. Will restart in a sec. To stop a vicious cycle `kill -9 %d' to terminate this watch dog.",
	      watch_dog_iteration, getpid());
	}
      }
      sleep(1); /* avoid spinning tightly */
    }
#endif
  }

 normal_child:
  D("Real server pid %d", getpid());

  if (kidpid_path) {
    int len;
    char buf[INTSTRLEN];
    len = sprintf(buf, "%d", (int)getpid());
    if (write_or_append_lock_c_path(pid_path, buf, len, "write pid", SEEK_SET, O_TRUNC) <= 0) {
      ERR("Failed to write kidpid file at `%s'. Check that all directories exist and that permissions allow dsproxy (pid=%d, euid=%d, egid=%d) to write the file. Disk could also be full or ulimit(1) too low. Continuing anyway.",
	  pid_path, getpid(), geteuid(), getegid());
    }
  }

#ifndef MINGW  
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {   /* Ignore SIGPIPE */
    perror("signal ignore pipe");
    exit(2);
  }

  /* Cause exit(3) to be called with the intent that any gcov profiling will get
   * written to disk before we die. If dsproxy is not stopped `kill -USR1' but you
   * use plain kill instead, the profile will indicate many unexecuted (#####) lines. */
  if (signal(SIGUSR1, exit) == SIG_ERR) {
    perror("signal USR1 exit");
    exit(2);
  }
#endif

  hit.shf = shuff = hi_new_shuffler(nfd, npdu);
  {
    struct hi_io* io;
    struct hi_host_spec* hs;
    struct hi_host_spec* hs_next;

    /* Prepare listeners first so we can then later connect to ourself. */
    CMDLINE("listen");

    for (hs = listen_ports; hs; hs = hs->next) {
      io = hi_open_listener(shuff, hs, hs->proto);
      if (!io) break;
      io->n = hs->conns;
      hs->conns = io;
    }
    
    for (hs = remotes; hs; hs = hs_next) {
      hs_next = hs->next;
      hs->next = prototab[hs->proto].specs;
      prototab[hs->proto].specs = hs;
      if (hs->proto == S5066_SMTP)
	continue;  /* SMTP connections are opened later, when actual data from SIS arrives. */

      if (hs->sin.sin_family == 0xfead)
	io = serial_init(hs);
      else
	io = hi_open_tcp(shuff, hs, hs->proto);
      if (!io) break;
      io->n = hs->conns;
      hs->conns = io;
      switch (hs->proto) {
      case S5066_SIS:   /* *** Always bind as HMTP. Make configurable. */
	sis_send_bind(&hit, io, SAP_ID_HMTP, 0, 0x0200);  /* 0x0200 == nonarq, no repeats */
	break;
      case S5066_DTS:
	ZMALLOC(io->ad.dts);
	io->ad.dts->remote_station_addr[0] = 0x61;   /* three nibbles long (padded with zeroes) */
	io->ad.dts->remote_station_addr[1] = 0x23;
	io->ad.dts->remote_station_addr[2] = 0x00;
	io->ad.dts->remote_station_addr[3] = 0x00;
	break;
      }
    }
  }

  if (snmp_port) {
#ifdef HAVE_NET_SNMP
    initializeSNMPSubagent("open5066", SNMPLOGFILE);
    /* *** we need to discover the SNMP socket somehow so we can insert it to
     * our file descriptor table so it gets properly polled, etc. --Sampo */
#else
    ERR("This binary was not compiled to support SNMP (%d). Continuing without.", snmp_port);
#endif
  }
  
  /* Drop privileges, if requested. */
  
  if (drop_gid) if (setgid(drop_gid)) { perror("setgid"); exit(1); }
  if (drop_uid) if (setuid(drop_uid)) { perror("setuid"); exit(1); }
  
  /* Unleash threads so that the listeners are served. */
  
  CMDLINE("unleash");
  {
    int err;
    pthread_t tid;
    for (--nthr; nthr; --nthr)
      if ((err = pthread_create(&tid, 0, thread_loop, shuff))) {
	ERR("pthread_create() failed: %d (nthr=%d)", err, nthr);
	exit(2);
      }
  }
  
  hi_shuffle(&hit, shuff);  /* main thread becomes one of the workers */
  return 0; /* never really happens because hi_shuffle() never returns */
}

char* assert_msg = "%s: Internal error caused an ASSERT to fire. Deliberately provoking a core dump.\nSorry for the inconvenience and thank you for your collaboration.\n";

/* EOF  --  s5066d.c */
