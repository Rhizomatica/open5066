/*
 * $Id: synccat.c,v 1.3 2006/06/18 00:44:51 sampo Exp $
 *
 * Simple main program to call dialout.c
 *
 * 28/4/2006 -- Nito@Qindel.ES
 * 17.6.2006, Sampo Kellomaki (sampo@iki.fi)
 */

/*#include "config.h"*/
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#ifdef HAVE_NET_SNMP
# include "snmpInterface.h"
#endif
#include "serial_sync.h"
#include "myconfig.h"
#include "errmac.h"

#define SUBAGENTNAME "open5066"
#define SNMPLOGFILE "/var/tmp/snmpOpen5066.log"

#define MYDEVICE "/dev/se_hdlc1"
#define MYFRAMESIZE 1000
#define MYBAUDRATE 11520


char* instance = "s5066d/serial_sync";
int debug = 1;
int continueRunning = 1;

void stop_server(int a);

int main(int argc, char **argv) {
  int fd, n, bytes_in, bytes_out, return_value = 0, 
    result, snmpfd, maxFD, block = 0;
  fd_set input;
  struct timeval timeout;
  char buffer[MAXBUFFER+1];
  
  signal(SIGTERM, stop_server);
  signal(SIGINT, stop_server);

  printf("opening device %s\n", MYDEVICE);
  fd = open_port(MYDEVICE); /* should check for -1 in a lot of places*/
  maxFD = fd + 1;
  /*  write_port(fd, MYDEVICE,"Hello World!\n", strlen("Hello World!\n"));*/
  FD_ZERO(&input);
#if HAVE_NET_SNMP
  initializeSNMPSubagent(SUBAGENTNAME, SNMPLOGFILE);
#endif

  log_port_info(fd, MYDEVICE, "before");
  set_baud_rate(fd, MYDEVICE, MYBAUDRATE);
  set_frame_size(fd, MYDEVICE, MYFRAMESIZE);
  set_serial_opts(fd, MYDEVICE);
  log_port_info(fd, MYDEVICE, "after");

  while (continueRunning) {
    FD_SET(fd, &input);
    FD_SET(STDIN_FILENO, &input);
    timeout.tv_sec = 20;
    timeout.tv_usec = 0;
#if HAVE_NET_SNMP
    /* NetSNMP is a bit fascist in this sense ... */
    n = selectAndUpdateSNMPFDSET(&maxFD, &input, &timeout);
    timeout.tv_sec = 20;
    timeout.tv_usec = 0;
#endif
    n = select(maxFD, &input, NULL, NULL, &timeout);


    if (n < 0) {
      perror("select failed");
      return_value = 1;
      break;
    } 
    if (n == 0) {
      puts("TIMEOUT");
      return_value = 2;
      break;
    }
    
    if (FD_ISSET(fd, &input)) { 
      if (!processSerial(fd, MYDEVICE)) {
	return_value = 3;
	break;
      }
    } else if (FD_ISSET(STDIN_FILENO, &input)) { 
      if (!processStdin(fd, MYDEVICE)) {
	return_value = 4;
	break;
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
  log_port_info(fd, MYDEVICE, "closing");
#if HAVE_NET_SNMP
  snmp_shutdown("snmpOpen5066");
#endif
  close_port(fd, MYDEVICE);
  return return_value;
}

/*
 * processStdin
 *
 * Input:
 * - filedescriptor of the serial port
 * - String with the name of the serial device
 * Output:
 * - 1 if no errors
 * - 0 if errors exist
 */
int processStdin(int fd, const char *device) {
  char buffer[MAXBUFFER+1];
  int bytes_read, bytes_written;

  bytes_read = read(STDIN_FILENO, buffer, MAXBUFFER);
  if (bytes_read == -1) {
    perror("Error reading STDIN...");
    return 0;
  }
  bytes_written = 0;
  while (bytes_written < bytes_read) {
    bytes_written += write_port(fd, MYDEVICE, buffer + bytes_written, bytes_read - bytes_written);
    if (bytes_written == -1) {
      perror("Error writing to serial port...");
      return 0;
    }
    buffer[bytes_written] = '\0';
    D("Read from stdin and written to fd %d port %s: <%s>", 
	   fd, device, buffer);
  }
  return 1;
}

/*
 * processSerial
 *
 * Input:
 * - filedescriptor of the serial port
 * - String with the name of the serial device
 * Output:
 * - 1 if no errors
 * - 0 if errors exist
 */
int processSerial(int fd, const char *device) {
  char buffer[MAXBUFFER+1];
  int bytes_read, bytes_written;

  bytes_read = read_port(fd, device, buffer, MAXBUFFER);
  if (bytes_read == -1) {
    perror("Error reading serial port...");
    return 0;
  }
  buffer[bytes_read] = '\0';
  D("Read from fd %d port %s: <%s>", 
	 fd, device, buffer);
  
  printf("%s", buffer);
  return 1;
}

void stop_server(int a) {
  continueRunning = 0;
  INFO("Server gracefully stopped", "");
}
