/* iocat - Raw Serial I/O Tool
 * $Id: iocat.c,v 1.7 2006/06/18 22:03:54 sampo Exp $
 *
 * 28/4/2006 -- Nito@Qindel.ES
 * 17.6.2006, Sampo Kellomaki (sampo@iki.fi)
 */

char* usage = 
"iocat - Raw Serial I/O Tool\n\
Usage: iocat -p serialport [opts] <input >output\n\
E.g:   iocat -p /dev/ttya -block <input >output\n\
E.g:   iocat -p /dev/se_hdlc1 -block -sbaud 9600 -sframe 64 -getmode -getmctl -getspeed <in >out\n\
Generally each option corresponds to an ioctl() to be set. There are no\n\
default settings other than those used by undelying operating system when\n\
opening a serial port.\n\
  -license   Display copyright, license, and (non)warranty information\n\
  -v         Verbose\n\
  -p dev     Open device. Usually the first thing, needed beofre other calls.\n\
  -pfd fd    Use inherited file descriptor\n\
  -block     Set fd in blocking mode (may be needed after open with O_NDELAY)\n\
  -nblock    Set fd in nonblocking mode.\n\
  -raw       Make sure the serial port is not in `cooked' mode\n\
\n\
  -tios      Get and print termios in short, raw hex, format\n\
  -c         Get and print termios control cflag symbolically\n\
    -baud 9600   Set baud rate\n\
    -bits 8N1    Set character size, parity, and stop bits\n\
    -rtscts      Enable outbound hardware flow control\n\
    -nrtscts     Disable outbound hardware flow control\n\
    -rtsxoff     Enable inbound hardware flow control\n\
    -nrtsxoff    Disable unbound hardware flow control\n\
x -l         Get and print termios local lflag symbolically\n\
x -i         Get and print termios input iflag symbolically\n\
x -o         Get and print termios output oflag symbolically\n\
\n\
  -cmget     Get modem control lines using ioctl(TIOCMGET)\n\
  -cmset N   Set modem control lines using ioctl(TIOCMSET). N is bitmask.\n\
  -dtr -ndtr Turn on/off  DTR (using ioctl(TIOCMBIS/BIC))\n\
  -dsr -ndsr Turn on/off  DSR\n\
  -rts -nrts Turn on  RTS\n\
  -cts -ncts Turn on  CTS\n\
  -cd  -ncd  Turn on  DCD\n\
  -ri  -nri  Turn on  RI\n\
  -softcar N Turn soft carrier on/off (i.e. ignore/respect DCD line)\n\
  -gsoftcar  Get soft carrier setting\n\
\n\
  -getmode   ioctl(S_IOCGETMODE) (sync serial stuff)\n\
  -getspeed  ioctl(S_IOCGETSPEED) (sync serial stuff)\n\
  -getmctl   ioctl(S_IOCGETMCTL)  (sync serial modem control lines)\n\
  -getstats  ioctl(S_IOCGETSTATS) (sync serial stuff)\n\
  -sbaud N   Set sync serial clocking and baud rate\n\
  -sframe N  Set sync serial frame size ioctl(S_IOCSETMRU)\n\
\n\
  -sleep N   Sleep N seconds\n\
  -usleep N  Sleep N microseconds\n\
  -w FOO     Write the specified characters to the port, retrying if needed\n\
  -wx 0xXX   Write octet(s) specified in hex to the port, retrying if needed\n\
  -r  N      Read maximum N characters and echo to stdout\n\
  -rx N      Read maximum N characters and echo to stdout as hex\n\
  -rd N      Read maximum N characters and discard\n\
x -R  N      Read N characters, retrying if needed, and echo to stdout\n\
x -Rx N      Read N characters, retrying if needed, and echo to stdout as hex\n\
x -Rd N      Read N characters, retrying if needed, and discard\n\
  -goto N    Continue processing at argument N\n\
  -nop       Do not do anything\n\
  -argi      Print current argument number (good for debugging)\n\
  -close     Close the fd (fd is automatically closed when program exits)\n\
  -exec ...  Exec another program. The other program inherits the fd\n\
(x = feature not implemented yet)\n\
See also: man termios, man termios, man stty\n\
http://www.ing.iac.es:8080/~docs/external/serial/serial.html\n";

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <termio.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <memory.h>
#ifdef HAVE_NET_SNMP
# include "snmpInterface.h"
#endif
#ifdef SUNOS
#include <sys/ser_sync.h>
#endif
#include "serial_sync.h"
#include "errmac.h"
#include "serial/dialout.h"

#ifndef PAREXT   /* Solaris specific */
#define PAREXT 0
#endif

#ifndef CBAUDEXT   /* Solaris specific */
#define CBAUDEXT 0
#endif

#ifndef CIBAUDEXT   /* Solaris specific */
#define CIBAUDEXT 0
#endif

#ifndef CRTSXOFF /* Solaris specific */
#define CRTSXOFF 0
#endif

#ifndef LOBLK   /* Solaris specific */
#define LOBLK 0
#endif

#ifndef CNEW_RTSCTS
#define CNEW_RTSCTS 0
#endif

#define DIE(reason,i) MB fprintf(stderr, "arg %d: %s\n%s", i, reason, usage); exit(2); ME
#define IOERR(what) MB fprintf(stderr, "%s: %d %s\n", what, errno, strerror(errno)); exit(3); ME
#define NREST1(a)          argv[argi][3] != a || argv[argi][4]
#define NREST2(a,b)        argv[argi][3] != a || argv[argi][4] != b || argv[argi][5]
#define NREST3(a,b,c)      argv[argi][3] != a || argv[argi][4] != b || argv[argi][5] != c || argv[argi][6]
#define NREST4(a,b,c,d)    argv[argi][3] != a || argv[argi][4] != b || argv[argi][5] != c || argv[argi][6] != d || argv[argi][7]
#define NREST5(a,b,c,d,e)  argv[argi][3] != a || argv[argi][4] != b || argv[argi][5] != c || argv[argi][6] != d || argv[argi][7] != e || argv[argi][8]

#define SUBAGENTNAME "open5066"
#define SNMPLOGFILE "/var/tmp/snmpOpen5066.log"

#define MYDEVICE "/dev/se_hdlc1"
#define MYFRAMESIZE 1000
#define MYBAUDRATE 11520

int snmp_port;
char* instance = "s5066d/iocat";
int debug = 1;
int verbose = 1;
int debugpoll = 0;
int gcthreshold = 0;
int watchdog = 0;
char* kidpid_path = 0;
int drop_uid = 0;
int drop_gid = 0;
int leak_free = 0;
int assert_nonfatal = 0;
int continueRunning = 1;
char* port = "none (use -o /dev/ttyXX to open)";
int fd = -1;

void stop_server(int a);

int write_all(int fd, char* buf, int len)
{
  int got,n;
  for (n = 0; n < len; n += got) {
    got = write(fd, buf + n, len - n);
    if (got == -1)
      return -1;
  }
  return len;
}

int clear_modem_ctl(int fd, int mask)
{
  int ret = ioctl(fd, TIOCMBIC, &mask);
  if (ret == -1) IOERR("ioctl(TIOCMBIC)");
  return ret;
}

int set_modem_ctl(int fd, int mask)
{
  int ret = ioctl(fd, TIOCMBIS, &mask);
  if (ret == -1) IOERR("ioctl(TIOCMBIS)");
  return ret;
}

int modify_tios_cflag(int fd, int and_mask, int or_mask)
{
  int ret;
  struct termios tios;
  memset(&tios, 0, sizeof(tios));
  D("tcgetattr(%d)", fd);
  ret = tcgetattr(fd, &tios);
  if (ret == -1) IOERR("tcgetattr");
  tios.c_cflag = tios.c_cflag & ~CRTSCTS;
  D("tcsetattr and=0x%x or=0x%x", and_mask, or_mask);
  ret = tcsetattr(fd, TCSANOW, &tios); /* vs. TCSADRAIN? */
  if (ret == -1) IOERR("tcsetattr");
  return 0;
}

int main(int argc, char **argv) {
  struct termio tio;
  struct termios tios;
  int argi, ret, n, bytes_in, bytes_out, exit_val = 0, snmpfd, maxFD, block = 0;
  fd_set input;
  struct timeval timeout;
  char buf[MAXBUFFER+1];
  
  signal(SIGTERM, stop_server);
  signal(SIGINT, stop_server);

#if HAVE_NET_SNMP
  initializeSNMPSubagent(SUBAGENTNAME, SNMPLOGFILE);
#endif

  for (argi = 1; argi < argc; ++argi) {
    if (argv[argi][0] != '-')
      DIE("Bad argument", argi);
    
    switch (argv[argi][1]) {
    case '-': if (argv[argi][2]) break;
      ++argi;
      DD("End of options by --");
      goto endofopts;

    case 'p':
      switch (argv[argi][2]) {
      case '\0':
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	port = argv[argi];
	D("Try open(%s, O_RDWR | O_NOCTTY | O_NDELAY)\n", port);
	fd=open(port, O_RDWR | O_NOCTTY | O_NDELAY); 
	if (fd == -1) {
	  ERR("arg %d: open(%s, O_RDWR | O_NOCTTY | O_NDELAY): %d %s\n", argi,
	      port, errno, strerror(errno));
	  exit(1);
	}
	D("open(%s, O_RDWR | O_NOCTTY | O_NDELAY) ok, fd(%d)\n", port, fd);
	maxFD = fd + 1;
	/*  write_port(fd, MYDEVICE,"Hello World!\n", strlen("Hello World!\n"));*/
	FD_ZERO(&input);
	continue;
      case 'f': if (argv[argi][3] != 'd' || argv[argi][4]) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	port = argv[argi];
	fd = atoi(port);
	D("inherited fd(%d)", fd);
	continue;
      }
      break;

    case 'b':
      switch (argv[argi][2]) {
      case 'l': if(NREST3('o','c','k')) break;
	D("%d: fcntl(F_SETFL) block", argi);
	ret = fcntl(fd, F_SETFL, 0); /* blocking read */
	if (ret == -1) IOERR("fcntl(F_SETFL) block");
	continue;
      case 'a': if(NREST2('u','d')) break;
	D("%d: tcgetattr", argi);
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	modify_tios_cflag(fd, ~CBAUD, hi_find_speed(argv[argi]));
	continue;
      case 'i': if(NREST2('t','s')) break;
	D("%d: tcgetattr", argi);
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	memset(&tios, 0, sizeof(tios));
	ret = tcgetattr(fd, &tios);
	if (ret == -1) IOERR("tcgetattr");
	switch (argv[argi][0]) {
	case '8': tios.c_cflag = tios.c_cflag & ~CSIZE | CS8; break;
	case '7': tios.c_cflag = tios.c_cflag & ~CSIZE | CS7; break;
	case '6': tios.c_cflag = tios.c_cflag & ~CSIZE | CS6; break;
	case '5': tios.c_cflag = tios.c_cflag & ~CSIZE | CS5; break;
	default: DIE("Bad character size", argi);
	}
	switch (argv[argi][1]) {
	case 'N': tios.c_cflag = tios.c_cflag & ~(PARENB | PARODD | PAREXT); break;
	case 'E': tios.c_cflag = tios.c_cflag & ~(PARODD | PAREXT) | PARENB; break;
	case 'O': tios.c_cflag = tios.c_cflag & ~PAREXT | PARENB | PARODD; break;
	case 'M': tios.c_cflag = tios.c_cflag & ~PARODD | PARENB | PAREXT; break;
	case 'S': tios.c_cflag = tios.c_cflag |  PARENB | PARODD | PAREXT; break;
	default: DIE("Bad parity spec", argi);
	}
	switch (argv[argi][1]) {
	case '2': tios.c_cflag = tios.c_cflag | CSTOPB; break;
	case '1': tios.c_cflag = tios.c_cflag & ~CSTOPB; break;
	default: DIE("Bad stop bit spec", argi);
	}
	tios.c_cflag = tios.c_cflag & ~CBAUD | hi_find_speed(argv[argi]);
	D("%d: tcsetattr", argi);
	ret = tcsetattr(fd, TCSANOW, &tios);
	if (ret == -1) IOERR("tcsetattr()");
	continue;
      }
      break;

    case 'n':
      switch (argv[argi][2]) {
      case 'b': if (NREST4('l','o','c','k')) break;
	D("%d: fcntl(F_SETFL, O_NONBLOCK | O_NDELAY) nonblock", argi);
	ret = fcntl(fd, F_SETFL, O_NONBLOCK | O_NDELAY); /* Return 0 if no chars available during read */
	if (ret == -1) IOERR("fcntl(F_SETFL, O_NONBLOCK | O_NDELAY) nonblock");
	continue;
      case 'o': if (NREST1('p')) break;
	continue;
      case 'r':
	if (!strcmp(argv[argi], "-nrtscts")) {
	  modify_tios_cflag(fd, ~CRTSCTS, 0);
	  continue;
	}
	if (!strcmp(argv[argi], "-nrtsxoff")) {
	  modify_tios_cflag(fd, ~CRTSXOFF, 0);
	  continue;
	}
	if (!strcmp(argv[argi], "-nrts")) { clear_modem_ctl(fd, TIOCM_RTS); continue; }
	if (!strcmp(argv[argi], "-nri"))  { clear_modem_ctl(fd, TIOCM_RI);  continue; }
	break;
      case 'd':
	if (!strcmp(argv[argi], "-ndtr")) { clear_modem_ctl(fd, TIOCM_DTR); continue; }
	if (!strcmp(argv[argi], "-ndsr")) { clear_modem_ctl(fd, TIOCM_DSR); continue; }
	break;
      case 'c':
	if (!strcmp(argv[argi], "-ncts")) { clear_modem_ctl(fd, TIOCM_CTS); continue; }
	if (!strcmp(argv[argi], "-ncd"))  { clear_modem_ctl(fd, TIOCM_CD);  continue; }
	break;
      }
      break;

    case 's':
      switch (argv[argi][2]) {
      case 'n': if (NREST2('m','p')) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	snmp_port = atoi(argv[argi]);
	continue;
      case 'l': if (NREST3('e','e','p')) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	n = atoi(argv[argi]);
	sleep(n);
	continue;
      case 'o':
	if (!strcmp(argv[argi], "-softcar")) {
	  ++argi;
	  if (argi >= argc) DIE("missing option argument", argi);
	  sscanf(argv[argi], "%i", &n);
	  ret = ioctl(fd, TIOCSSOFTCAR, &n);
	  if (ret == -1) IOERR("ioctl(TIOCSSOFTCAR)");
	  continue;
	}
	break;
      case 'b':
	if (!strcmp(argv[argi], "-sbaud")) {
	  ++argi;
	  if (argi >= argc) DIE("missing option argument", argi);
	  sscanf(argv[argi], "%i", &n);
	  ret = set_baud_rate(fd, port, n);
	  if (ret == -1) IOERR("set_baud_rate()");
	  continue;
	}
	break;
      case 'f':
	if (!strcmp(argv[argi], "-sframe")) {
	  ++argi;
	  if (argi >= argc) DIE("missing option argument", argi);
	  sscanf(argv[argi], "%i", &n);
#ifdef SUNOS
	  ret = ioctl(fd, S_IOCSETMRU, &n);
#else
	  ERR("%d: ioctl not supported", argi);
#endif
	  if (ret == -1) IOERR("ioctl(S_IOCSETMRU)");
	  continue;
	}
	break;
      }
      break;

    case 't': if (argv[argi][2]) break;
      switch (argv[argi][2]) {
      case '\0':
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	timeout.tv_sec = atoi(argv[argi]);
	timeout.tv_usec = 0;
	continue;
      case 'i': if (NREST2('o','s')) break;
	D("%d: tcgetattr", argi);
	memset(&tios, 0, sizeof(tios));
	ret = tcgetattr(fd, &tios);
	if (ret == -1) IOERR("tcgetattr");
	fprintf(stderr, "tcgetaddr(%d) (%s)\n\tcflag=0x%x\n\tlflag=0x%x\n"
		"\tiflag=0x%x\n\toflag=0x%x\n"
#ifndef SUNOS
		"\tispeed=0x%x\n\tospeed=0x%x\n"
#endif
		"\tcontrol chars: ",
		fd, port,
		tios.c_cflag, tios.c_lflag, tios.c_iflag, tios.c_oflag
#ifndef SUNOS
		, tios.c_ispeed, tios.c_ospeed
#endif
		);
	for (n = 0; n < NCCS; ++n)
	  fprintf(stderr, " 0x%x", tios.c_cc[n]);
	fprintf(stderr, "\n");
	continue;
      }
      break;

    case 'c':
      switch (argv[argi][2]) {
      case '\0':
	D("%d: tcgetattr", argi);
	memset(&tios, 0, sizeof(tios));
	ret = tcgetattr(fd, &tios);
	if (ret == -1) IOERR("tcgetattr");
	
	fprintf(stderr, "tcgetaddr(%d) (%s) cflag=0x%x\n\tcbaud=0x%x %s\n"
	       "\texta=%x\n\textb=%x\n\tcsize=0x%x %s\n\tcstopb=%x\n"
	       "\tcread=%x\n\tparena=%x\n\tparodd=%x\n\thup=%x\n"
	       "\tclocal=%x\n\tloblk=%x\n\tcrtscts=%x\n\tcnew_rtscts=%x\n"
	       "\tcrtsxoff=%x\n\tcibaud=%x\n\tparext=%x\n"
	       "\tcbaudext=%x\n\tcibaudext=%x\n",
	       fd, port, tios.c_cflag,
	       tios.c_cflag & CBAUD,
	       hi_flag_desc(hi_flag_lookup(speednames, tios.c_cflag & CBAUD)),
	       tios.c_cflag & EXTA, tios.c_cflag & EXTB,
	       tios.c_cflag & CSIZE,
	       hi_flag_desc(hi_flag_lookup(cflagsnames, tios.c_cflag & CSIZE)),
	       tios.c_cflag & CSTOPB, tios.c_cflag & CREAD,
	       tios.c_cflag & PARENB, tios.c_cflag & PARODD, tios.c_cflag & HUPCL,
	       tios.c_cflag & CLOCAL, tios.c_cflag & LOBLK,
	       tios.c_cflag & CRTSCTS, tios.c_cflag & CNEW_RTSCTS,
	       tios.c_cflag & CRTSXOFF, tios.c_cflag & CIBAUD, tios.c_cflag & PAREXT,
	       tios.c_cflag & CBAUDEXT, tios.c_cflag & CIBAUDEXT);
	for (n = 0; n < NCCS; ++n)
	  fprintf(stderr, " 0x%x", tios.c_cc[n]);
	fprintf(stderr, "\n");
	continue;
      case 'l': if (NREST3('o','s','e')) break;
	close(fd);
	continue;
      case 'm':
	if (!strcmp(argv[argi], "-cmget")) {
	  n = 0;
	  ret = ioctl(fd, TIOCMGET, &n);
	  if (ret == -1) IOERR("ioctl(TIOCMGET)");
	  fprintf(stderr, "ioctl(TIOCMGET) 0x%x\n"
		 "\tLineEnable=%x\n\tDTR=%x\n\tRTS=%x\n"
		 "\tSecondTx=%x\n\tSecondRx=%x\n\tCTS=%x\n"
		 "\tCD=%x\n\tRI=%x\n\tDSR=%x\n", n,
		 n & TIOCM_LE, n & TIOCM_DTR, n & TIOCM_RTS,
		 n & TIOCM_ST, n & TIOCM_SR, n & TIOCM_CTS,
		 n & TIOCM_CD, n & TIOCM_RI, n & TIOCM_DSR);
	  continue;
	}
	if (!strcmp(argv[argi], "-cmset")) {
	  ++argi;
	  if (argi >= argc) DIE("missing option argument", argi);
	  sscanf(argv[argi], "%i", &n);
	  ret = ioctl(fd, TIOCMSET, &n);
	  if (ret == -1) IOERR("ioctl(TIOCMSET)");
	  continue;
	}
	break;
      case 'd': if (argv[argi][3]) break;  set_modem_ctl(fd, TIOCM_CD);  continue;
      case 't': if (NREST1('s')) break;    set_modem_ctl(fd, TIOCM_CTS); continue;
      }
      break;

    case 'a':
      switch (argv[argi][2]) {
      case 'r': if (NREST2('g','i')) break;
	fprintf(stderr, "%d: argi\n", argi);
	continue;
      }
      break;

    case 'g':
      switch (argv[argi][2]) {
      case 'o': if (NREST2('t','o')) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	argi = atoi(argv[argi]) - 1;
	continue;
      case 's':
	if (!strcmp(argv[argi], "-gsoftcar")) {
	  n = 0;
	  ret = ioctl(fd, TIOCGSOFTCAR, &n);
	  if (ret == -1) IOERR("ioctl(TIOCGSOFTCAR)");
	  fprintf(stderr, "softcarrier=0x%x\n", n);
	  continue;
	}
	break;
      case 'e':
	if (!strcmp(argv[argi], "-getmode")) {
#ifdef SUNOS
	  struct scc_mode mode;
	  ret = ioctl(fd, S_IOCGETMODE, &mode);
	  if (ret == -1) IOERR("ioctl(S_IOCGETMODE)");
	  fprintf(stderr, "ioctl(S_IOCGETMODE)\n"
		  "\ttx clk src=%x\n\trx clk src=%x\n"
		  "\tdata and clock inversion flags=%x\n\tconfig opts=%x\n"
		  "\tbaud rate=%d\n\treason code=%d\n",
		  mode.sm_txclock,  mode.sm_rxclock,
		  mode.sm_iflags,   mode.sm_config,
		  mode.sm_baudrate, mode.sm_retval);
#else
	  ERR("%d: ioctl not supported", argi);
#endif
	  continue;
	}
	if (!strcmp(argv[argi], "-getspeed")) {
#ifdef SUNOS
	  ret = ioctl(fd, S_IOCGETSPEED, &n);
	  if (ret == -1) IOERR("ioctl(S_IOCGETSPEED)");
	  fprintf(stderr, "ioctl(S_IOCGETSPEED)=%d\n", n);
#else
	  ERR("%d: ioctl not supported", argi);
#endif
	  continue;
	}
	if (!strcmp(argv[argi], "-getmctl")) {
#ifdef SUNOS
	  int cts, dcd;
	  ret = ioctl(fd, S_IOCGETMCTL, &cts, &dcd);
	  if (ret == -1) IOERR("ioctl(S_IOCGETMCTL)");
	  fprintf(stderr, "ioctl(S_IOCGETMCTL) cts=%x dcd=%x\n", cts, dcd);
#else
	  ERR("%d: ioctl not supported", argi);
#endif
	  continue;
	}
	if (!strcmp(argv[argi], "-getstats")) {
#ifdef SUNOS
	  struct sl_stats st;
	  ret = ioctl(fd, S_IOCGETSTATS, &st);
	  if (ret == -1) IOERR("ioctl(S_IOCGETSTATS)");
	  fprintf(stderr, "ioctl(S_IOCGETSTATS)\n"
		  "\tipack=%d\topack=%d\n"
		  "\tichar=%d\tochar=%d\n"
		  "\tabort=%d\tcrc=%d\n"
		  "\tcts=%d\tdcd=%d\n"
		  "\toverrun=%d\tunderrun=%d\n"
		  "\tierror=%d\toerror=%d\n"
		  "\tnobuffers=%d\n",
		  st.ipack, st.opack, st.ichar, st.ochar,
		  st.abort, st.crc, st.cts, st.dcd,
		  st.overrun, st.underrun, st.ierror, st.oerror, st.nobuffers);
#else
	  ERR("%d: ioctl not supported", argi);
#endif
	  continue;
	}
	break;
      }
      break;

    case 'e':
      switch (argv[argi][2]) {
      case 'x': if (NREST2('e','c')) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	execvp(argv[argi], argv+argi);
	IOERR("execv");
      }
      break;

    case 'd':
      switch (argv[argi][2]) {
      case '\0':
	++debug;
	continue;
      case 'p':  if (argv[argi][3]) break;
	++debugpoll;
	continue;
      case 'i':  if (argv[argi][3]) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	instance = argv[argi];
	continue;
      case 't': if(NREST1('r')) break; set_modem_ctl(fd, TIOCM_DTR); continue;
      case 's': if(NREST1('r')) break; set_modem_ctl(fd, TIOCM_DSR); continue;
      }
      break;

    case 'v':
      switch (argv[argi][2]) {
      case '\0':
	++verbose;
	continue;
      }
      break;

    case 'q':
      switch (argv[argi][2]) {
      case '\0':
	verbose = 0;
	continue;
      }
      break;

    case 'r':
      switch (argv[argi][2]) {
      case 'f':
	/*AFR_TS(LEAK, 0, "memory leaks enabled");*/
#if 1
	ERR("*** WARNING: You have turned memory frees to memory leaks. We will (eventually) run out of memory. Using -rf is not recommended. %d\n", 0);
#endif
	++leak_free;
	continue;
#if 0
      case 'e':	if (argv[argi][3]) break;
	if (argi + 4 >= argc) DIE("missing option argument", argi);
	sscanf(argv[argi+1], "%i", &abort_funcno);
	sscanf(argv[argi+2], "%i", &abort_line);
	sscanf(argv[argi+3], "%i", &abort_error_code);
	sscanf(argv[argi+4], "%i", &abort_iter);
	fprintf(stderr, "Will force core upon %x:%x err=%d iter=%d\n",
		abort_funcno, abort_line, abort_error_code, abort_iter);
	argi += 5;
	continue;
#endif
      case 'g':	if (argv[argi][3]) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	gcthreshold = atoi(argv[argi]);
	if (!gcthreshold)
	  ERR("*** WARNING: You have disabled garbage collection. This may lead to increased memory consumption for scripts that handle a lot of PDUs or run for long time. Using `-rg 0' is not recommended. %d\n", 0);
	continue;
      case 'a':
	if (argv[argi][3] == 0) {
	  /*AFR_TS(ASSERT_NONFATAL, 0, "assert nonfatal enabled");*/
#if 1
	  ERR("*** WARNING: YOU HAVE TURNED ASSERTS OFF USING -ra FLAG. THIS MEANS THAT YOU WILL NOT BE ABLE TO OBTAIN ANY SUPPORT. IF PROGRAM NOW TRIES TO ASSERT IT MAY MYSTERIOUSLY AND UNPREDICTABLY CRASH INSTEAD, AND NOBODY WILL BE ABLE TO FIGURE OUT WHAT WENT WRONG OR HOW MUCH DAMAGE MAY BE DONE. USING -ra IS NOT RECOMMENDED. %d\n", assert_nonfatal);
#endif
	  ++assert_nonfatal;
	  continue;
	}
	if (NREST1('w')) break;
	D("%d: tcgetattr raw", argi);
	memset(&tios, 0, sizeof(tios));
	ret = tcgetattr(fd, &tios);
	if (ret == -1) IOERR("tcgetattr");
	
	tios.c_cflag |= CLOCAL | CREAD;
	tios.c_lflag &= ~(ICANON | ECHO | ECHOE);
	tios.c_iflag &= ~(IXON | IXOFF | IXANY);
	tios.c_oflag &= ~OPOST;
	
	D("%d: tcsetattr raw", argi);
	ret = tcsetattr(fd, TCSANOW, &tios);
	if (ret == -1) IOERR("tcsetattr()");
	continue;

      case '\0':
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	sscanf(argv[argi], "%i", &n);
	if (n > sizeof(buf))
	  DIE("read size exceeds internal buffer size", argi);
	ret = read(fd, buf, n);
	switch (ret) {
	case -1: IOERR("read");
	case 0: D("EOF %d", n); break;
	default:
	  D("read got %d chars", ret);
	  printf("%.*s", ret, buf);
	}
	continue;
      case 'x':
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	sscanf(argv[argi], "%i", &n);
	if (n > sizeof(buf))
	  DIE("read size exceeds internal buffer size", argi);
	ret = read(fd, buf, n);
	switch (ret) {
	case -1: IOERR("read");
	case 0: D("EOF %d", n); break;
	default:
	  D("read got %d chars", ret);
	  for (n = 0; n < ret; ++n)
	    printf("0x%02x ", buf[n]);
	  printf("\n");
	}
	continue;
      case 'd':
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	sscanf(argv[argi], "%i", &n);
	if (n > sizeof(buf))
	  DIE("read size exceeds internal buffer size", argi);
	ret = read(fd, buf, n);
	switch (ret) {
	case -1: IOERR("read");
	case 0: D("EOF %d", n); break;
	default:
	  D("read got %d chars (discarded)", ret);
	}
	continue;
      case 't':
	if (!strcmp(argv[argi], "-rtscts")) {
	  modify_tios_cflag(fd, ~0, CRTSCTS);
	  continue;
	}
	if (!strcmp(argv[argi], "-rtsxoff")) {
	  modify_tios_cflag(fd, ~0, CRTSXOFF);
	  continue;
	}
	if (!strcmp(argv[argi], "-rts")) { set_modem_ctl(fd, TIOCM_RTS);  continue; }
	break;
      case 'i': if (argv[argi][3]) break; set_modem_ctl(fd, TIOCM_RI);  continue;
      }
      break;

    case 'w':
      switch (argv[argi][2]) {
      case 'a':
	if (!strcmp(argv[argi],"-watchdog")) {
	  ++watchdog;
	  continue;
	}
	break;
      case '\0':
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	n = write_all(fd, argv[argi], strlen(argv[argi]));
	if (n == -1)
	  IOERR("write");
	continue;
      case 'x': if (argv[argi][3]) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);

	for (n = 0; argv[argi][0] != '-' && n < sizeof(buf); ++n) {
	  sscanf(argv[argi], "%i", &ret);
	  buf[n] = ret;
	  ++argi;
	  if (argi >= argc)
	    break;
	}
	
	n = write_all(fd, buf, n);
	if (n == -1)
	  IOERR("write");
	continue;
      }
      break;

    case 'k':
      switch (argv[argi][2]) {
      case 'i':
	if (!strcmp(argv[argi],"-kidpid")) {
	  ++argi;
	  if (argi >= argc) DIE("missing option argument", argi);
	  kidpid_path = argv[argi];
	  continue;
	}
	break;
      }
      break;

    case 'u':
      switch (argv[argi][2]) {
      case 'i': if (argv[argi][3] != 'd' || argv[argi][4]) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	sscanf(argv[argi], "%i:%i", &drop_uid, &drop_gid);
	continue;
      case 's': if (NREST4('l','e','e','p')) break;
	++argi;
	if (argi >= argc) DIE("missing option argument", argi);
	n = atoi(argv[argi]);
	usleep(n);
	continue;
      }
      break;

    case 'l':
      switch (argv[argi][2]) {
      case 'i':
	if (!strcmp(argv[argi],"-license")) {
	  extern char* license;
	  fprintf(stderr, license);
	  exit(0);
	}
	break;
      }
      break;

    } 
    /* fall thru means unrecognized flag */
    if (argc)
      fprintf(stderr, "Unrecognized flag `%s'\n", argv[argi]);
  argerr:
    DIE("Bad arg", argi);
  }

 endofopts:

#if 0  
  log_port_info(fd, port, "before");
  set_baud_rate(fd, port, MYBAUDRATE);
  set_frame_size(fd, port, MYFRAMESIZE);
  set_serial_opts(fd, port);
  log_port_info(fd, port, "after");
#endif

  if (fd < 0) {
    fprintf(stderr, "file descriptor closed, or never opened (see -p option)\n");
    exit(0);
  }

  /* Main loop */
  
  while (continueRunning) {
    FD_SET(fd, &input);
    FD_SET(STDIN_FILENO, &input);
#if HAVE_NET_SNMP
    /* NetSNMP is a bit fascist in this sense ... */
    n = selectAndUpdateSNMPFDSET(&maxFD, &input, &timeout);
    timeout.tv_sec = 20;
    timeout.tv_usec = 0;
#endif
    n = select(maxFD, &input, 0, 0, 0);

    if (n < 0) {
      perror("select failed");
      exit_val = 1;
      break;
    } 
    if (n == 0) {
      puts("TIMEOUT");
      exit_val = 2;
      break;
    }
    
    if (FD_ISSET(fd, &input)) { 
      n = read(fd, buf, sizeof(buf));
      switch (n) {
      case -1: IOERR("read from fd");
      case 0:  D("EOF seen from fd(%d)", fd);
      default:
	DD("read %d chars", n);
	printf("%.*s", n, buf);
      }
    } else if (FD_ISSET(STDIN_FILENO, &input)) { 
      n = read(STDIN_FILENO, buf, sizeof(buf));
      switch (n) {
      case -1: IOERR("read from stdin");
      case 0:  D("EOF seen from STDIN(%d)", STDIN_FILENO);
      default:
	n = write_all(fd, buf, n);
	if (n == -1)
	  IOERR("write to port");
	D("wrote %d chars(%.*s)", n, n, buf);
      }
    } else { 
#if HAVE_NET_SNMP
      /* Not sure how to check that there were no errors... */
      processSNMP(&input);
#else
      puts("We should never get here");
#endif
    }
#if HAVE_NET_SNMP
    /* Send snmp alarms */
    otherSNMPTasks();
#endif
  }
  log_port_info(fd, port, "closing");
#if HAVE_NET_SNMP
  snmp_shutdown("snmpOpen5066");
#endif
  close_port(fd, port);
  return exit_val;
}

void stop_server(int a) {
  continueRunning = 0;
  INFO("Server gracefully stopped", "");
}

/* EOF -- iocat.c */
