/*
 * $Id: serial_sync.c,v 1.4 2006/06/18 21:06:31 sampo Exp $
 *
 * Simple C program to do a connection to a serial port and output it
 * to stdout and log it to syslog
 *
 * 28/4/2006 -- Nito@Qindel.ES
 * 17.6.2006, further hacking by Sampo Kellomaki (sampo@iki.fi)
 */
//#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termio.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <errno.h>
#if defined (__SVR4) && defined (__sun)
#include <sys/ser_sync.h>
#endif
#include "serial_sync.h"
#include "globalcounter.h"
#include "errmac.h"

int open_port(const char *file) {
  int fd;
  
  fd=open(file, O_RDWR | O_NOCTTY | O_NDELAY); 
  if (fd == -1) {
    ERR("open_port: Error opening device <%s> with error: %s", file, strerror(errno));
    return -1;
  }

  INFO("open_port: Successfully opened device <%s> with fd: %d", file, fd);
  /* fcntl(fd, F_SETFL, FNDELAY); */ /* Return 0 if no chars available during read */
  fcntl(fd, F_SETFL, 0); /* blocking read */

  return fd;
}

void close_port(int fd, const char *file) {
  int error;

  error = close(fd);
  if (error == -1) {
    ERR("close_port: Error closing filedescriptor %d of device <%s> with error: %s", fd, file, strerror(errno));
  } else {
    D("close_port: Successfully closed device <%s> with fd %d", file, fd);
  }
}

int write_port(int fd, const char *file, const void *buffer, int size) {
  int written;

  written = write(fd, buffer, size);
  if (written == -1) {
    ERR("write_port: Error writing to fd %d file <%s> with error: %s",
	   fd, file, strerror(errno));
    add_to_globalcounter(SERIAL_IF_WRITE_ERRORS,1);
    return -1;
  }
  add_to_globalcounter(SERIAL_IF_WRITE_BYTES, written);

  D("write_port: Written %d bytes to fd %d file <%s>", written, fd, file);
  
  return written;
}

int read_port(int fd, const char *file, void *buffer, int size) {
  ssize_t read_bytes;

  read_bytes = read(fd, buffer, size);
  if (read_bytes == -1) {
    ERR("read_port: Error reading from fd %d file <%s> with error: %s",
	   fd, file, strerror(errno));
    add_to_globalcounter(SERIAL_IF_READ_ERRORS,1);
    return -1;
  }

  add_to_globalcounter(SERIAL_IF_READ_BYTES, read_bytes);
  D("read_port: Read %d bytes from fd %d file <%s>", read, fd, file);
  
  return read_bytes;
}

int log_port_info(int fd, const char *file, char* logkey) {
  int speed, cts, dcd, tmp;
#if defined (__SVR4) && defined (__sun)
  struct scc_mode mode;
  struct sl_stats stats;
  /* Get stats. See
   * man -s 7I termio  -- Gneral async options
   * man -s 7D se_hdlc -- Sync serial specifics, including S_IOCGETMODE */
  if (ioctl(fd, S_IOCGETMODE, &mode) < 0) {
    ERR("%s: Error reading from fd %d file <%s> with error: %s",
	   logkey, fd, file, strerror(errno));
    return -1;
  }

  INFO("%s: Statistics for fd %d device %s", logkey, fd, file);
  INFO("%s: Transmit clock source: %d", logkey, mode.sm_txclock);
  INFO("%s: Receive clock source: %d", logkey,  mode.sm_rxclock);
  INFO("%s: Data and clock inversion flags: %d", logkey, mode.sm_iflags);
  INFO("%s: Boolean configuration options: %d", logkey, mode.sm_config);
  INFO("%s: Baud rate: %d", logkey, mode.sm_baudrate);
  INFO("%s: Reason codes for ioctl failures: %d", logkey, mode.sm_retval);

  if (ioctl(fd, S_IOCGETSPEED, &speed)) {
    ERR("%s: Error reading from fd %d file <%s> with error: %s", logkey,
	   fd, file, strerror(errno));
    return -1;
  }
  INFO("%s: Speed: %d", logkey, speed);
  
  if (ioctl(fd, S_IOCGETMCTL, &cts, &dcd)) {
    ERR("%s: Error reading from fd %d file <%s> with error: %s", logkey,
	   fd, file, strerror(errno));
    return -1;
  }
  INFO("%s: CTS and DCD: %x %x", logkey, cts, dcd);

  if (ioctl(fd, S_IOCGETSTATS, &stats) < 0) {
    ERR("%s: Error reading from fd %d file <%s> with error: %s", logkey,
	   fd, file, strerror(errno));
    return -1;
  }
  INFO("%s: driver ipack = %d", logkey, stats.ipack);
  INFO("%s: driver opack = %d", logkey, stats.opack);
  INFO("%s: driver ichar = %d", logkey, stats.ichar);
  INFO("%s: driver ochar = %d", logkey, stats.ochar);
  INFO("%s: driver abort = %d", logkey, stats.abort);
  INFO("%s: driver crc = %d", logkey, stats.crc);
  INFO("%s: driver cts = %d", logkey, stats.cts);
  INFO("%s: driver dcd = %d", logkey, stats.dcd);
  INFO("%s: driver overrun = %d", logkey, stats.overrun);
  INFO("%s: driver underrun = %d", logkey, stats.underrun);
  INFO("%s: driver ierror = %d", logkey, stats.ierror);
  INFO("%s: driver oerror = %d", logkey, stats.oerror);
  INFO("%s: driver nobuffers = %d", logkey, stats.nobuffers);

#endif

#if 0
  if (ioctl(fd, TIOCMGET, &tmp) < 0) {
    ERR("%s: Error ioctl(%d, TIOCMGET) <%s> with error: %s", logkey,
	   fd, file, strerror(errno));
    return -1;
  }
  INFO("%s: TIOCMGET(%d) 0x%x\n\tLinEna=%x\n\tDTR=%x\n\tRTS=%x\n\tSecTx=%x\n\tSecRx=%x\n\tCTS=%x\n\tCD=%x\n\tRI=%x\n\tDSR=%x\n", logkey,
       tmp,
       tmp & TIOCM_LE,
       tmp & TIOCM_DTR,
       tmp & TIOCM_RTS,
       tmp & TIOCM_ST,
       tmp & TIOCM_SR,
       tmp & TIOCM_CTS,
       tmp & TIOCM_CD,
       tmp & TIOCM_RI,
       tmp & TIOCM_DSR);
#endif

  INFO("%s: Bytes read %d, written %d", logkey,
       get_globalcounter(SERIAL_IF_WRITE_BYTES), 
       get_globalcounter(SERIAL_IF_READ_BYTES));

  return 0;
}

int set_baud_rate(int fd, const char *file, int baudrate) {
#if defined (__SVR4) && defined (__sun)
  struct scc_mode mode;

  mode.sm_baudrate = baudrate;
  mode.sm_txclock = TXC_IS_BAUD;
  mode.sm_rxclock = RXC_IS_BAUD;
  mode.sm_config = CONN_NRZI;
  mode.sm_iflags = 0;

  if (ioctl(fd, S_IOCSETMODE, &mode) < 0) {
    if (ioctl(fd, S_IOCSETMODE, &mode) < 0) 
      mode.sm_retval=0;
    ERR("set_baud_rate: Error setting mode on fd %d file %s with error code %d for baudrate %d (see IOCSETMODE errors in /usr/include/sys/ser_sync.h)",
	fd, file, mode.sm_retval, baudrate);
    return -1;
  }
  set_globalcounter(SERIAL_IF_SPEED,baudrate);
  INFO("set_baud_rate: Set baud rate to %d on fd %d file %s. Encoding NRZI, and  clock is baud generated.",
	 baudrate, fd, file);
#else
  ERR("Currently only implemented for Solaris","\n");
#endif
  return 0;
}

int set_frame_size(int fd, const char *file, int frame_size) {
#if defined (__SVR4) && defined (__sun)
  if (ioctl(fd, S_IOCSETMRU, &frame_size) < 0) {
    ERR("set_frame_size: Error setting frame_size to %d on fd %d file %s",
	   fd, file, frame_size);
    return -1;
  }
  set_globalcounter(SERIAL_IF_MTU,frame_size);
  INFO("set_frame_size: Set frame_size to %d on fd %d file %s. Encoding NRZI, and  clock is baud generated.",
	 frame_size, fd, file);
#else
  ERR("Currently only implemented for Solaris","\n");
#endif
  return 0;
}

int set_serial_opts(int fd, const char *file) {
  int tmp = TIOCM_DTR | TIOCM_RTS;
#if defined (__SVR4) && defined (__sun)
  if (ioctl(fd, TIOCMSET, &tmp) < 0) {
    ERR("Error setting serial opts fd(%d) file(%s) opts=0x%x",
	   fd, file, tmp);
    return -1;
  }
#else
  ERR("Currently only implemented for Solaris","\n");
#endif
  return 0;
}


int reset_serial_counters(int fd, const char *file) {
  reset_allglobalcounters();

#if defined (__SVR4) && defined (__sun)
  if (ioctl(fd, S_IOCCLRSTATS) < 0) {
    INFO("log_port_info: Error resetting stats for fd %d file <%s> with error: %s",
	   fd, file, strerror(errno));
    return -1;
  }
#else
  ERR("Currently only implemented for Solaris","\n");
#endif

  return 0;
}

int update_serial_counters(int fd, const char *file) {
#if defined (__SVR4) && defined (__sun)

  struct scc_mode mode;
  struct sl_stats stats;
  int speed, cts, dcd;
  /* Get stats */
  set_globalcounter(SERIAL_IF_ADMIN_STATUS,1);
  set_globalcounter(SERIAL_IF_OPER_STATUS,1);

  if (ioctl(fd, S_IOCGETSPEED, &speed)) {
    ERR("update_serial_counters: Error reading from fd %d file <%s> with error: %s",
	   fd, file, strerror(errno));
    set_globalcounter(SERIAL_IF_OPER_STATUS,2);
    return -1;
  }
  D("update_serial_counters: Speed: %d", speed);
  set_globalcounter(SERIAL_IF_SPEED,speed);
  
  if (ioctl(fd, S_IOCGETSTATS, &stats) < 0) {
    ERR("update_serial_counters: Error reading from fd %d file <%s> with error: %s",
	   fd, file, strerror(errno));
    set_globalcounter(SERIAL_IF_OPER_STATUS,2);
    return -1;
  }
  set_globalcounter(SERIAL_IF_IN_PKTS,stats.ipack);
  D("update_serial_counters: driver ipack = %d", stats.ipack);
  set_globalcounter(SERIAL_IF_OUT_PKTS,stats.opack);
  D("update_serial_counters: driver opack = %d", stats.opack);
  set_globalcounter(SERIAL_IF_IN_OCTETS,stats.ichar);
  D("update_serial_counters: driver ichar = %d", stats.ichar);
  set_globalcounter(SERIAL_IF_OUT_OCTETS,stats.ochar);
  D("update_serial_counters: driver ochar = %d", stats.ochar);
  set_globalcounter(SERIAL_IF_ABORT_SIGNALS,stats.abort);
  D("update_serial_counters: driver abort = %d", stats.abort);
  set_globalcounter(SERIAL_IF_CRC_ERRORS,stats.crc);
  D("update_serial_counters: driver crc = %d", stats.crc);
  set_globalcounter(SERIAL_IF_CTS_TIMEOUTS,stats.cts);
  D("update_serial_counters: driver cts = %d", stats.cts);
  set_globalcounter(SERIAL_IF_CARRIER_DROPS,stats.dcd);
  D("update_serial_counters: driver dcd = %d", stats.dcd);
  set_globalcounter(SERIAL_IF_RCVR_OVERRUN,stats.overrun);
  D("update_serial_counters: driver overrun = %d", stats.overrun);
  set_globalcounter(SERIAL_IF_TX_UNDERRUN,stats.underrun);
  D("update_serial_counters: driver underrun = %d", stats.underrun);
  set_globalcounter(SERIAL_IF_IN_ERRORS,stats.ierror);
  D("update_serial_counters: driver ierror = %d", stats.ierror);
  set_globalcounter(SERIAL_IF_OUT_ERRORS,stats.oerror);
  D("update_serial_counters: driver oerror = %d", stats.oerror);
  set_globalcounter(SERIAL_IF_RCVR_NO_BUFFERS,stats.nobuffers);
  D("update_serial_counters: driver nobuffers = %d", stats.nobuffers);
#else
  ERR("Currently only implemented for Solaris","\n");
#endif
}
