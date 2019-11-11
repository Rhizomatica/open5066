/*
 * $Id: serial_sync.h,v 1.2 2006/06/18 00:40:18 sampo Exp $
 *
 * Simple prototype for dialout
 *
 * 28/4/2006 -- Nito@Qindel.ES
 */
#ifndef _SERIAL_SYNC_H
#define _SERIAL_SYNC_H 1

#define MAXBUFFER 1024

/* 
 * open_port
 *
 * Input: 
 * - Character string indicating a serial device"
 * Output:
 * - File descriptor or -1 in case of an error (errno is set).
 */
int open_port(const char *);

/*
 * close_port
 *
 * Always try to close the port to see if during the close an IO 
 * error happens (flush of last data).
 *
 * Input:
 * - Integer. File descriptor as returned by open_port
 * - Character string indicating the serial device
 */
void close_port(int fd, const char *file);

/*
 * write_port
 *
 * Input:
 * - The file descriptor
 * - The file string (useful in debugging)
 * - The format string (see printf)
 * - Optionally the arguments to the format string
 * Output:
 * - the number of bytes written or -1 in case of error. The error could be an 
 *   IO error or that the arguments to write have exceeded the MAXBUFFER size
 */
int write_port(int fd, const char *file, const void *buffer, int size);


/*
 * read_port
 *
 * Input:
 * - The file descriptor
 * - The file string (useful in debugging)
 * - The format string (see printf)
 * - Optionally the arguments to the format string
 * Output:
 * - the number of bytes written or -1 in case of error. The error could be an 
 *   IO error or that the arguments to write have exceeded the MAXBUFFER size
 */
int read_port(int fd, const char *file, void *buffer, int size);

/*
 * log_port_info
 *
 * Outputs to the log the options of the serial port
 *
 * Input:
 * - The file descriptor
 * - The serial device
 * Output:
 * - 0 on success, and 1 in case of error
 */
int log_port_info(int fd, const char *file, char* logkey);

/*
 * set_baud_rate
 *
 * Sets the baud speed. It sets also the clocking to baud generated
 * and the encoding to NRZI.
 *
 * Input:
 * - The file descriptor
 * - The serial device
 * - The speed that you want to set
 * Output:
 * - 0 on success, and 1 in case of error
 */
int set_baud_rate(int fd, const char *file, int baudrate);

/*
 * set_frame_size
 *
 * Sets the frame size
 *
 * Input:
 * - The file descriptor
 * - The serial device
 * - The speed that you want to set
 * Output:
 * - 0 on success, and 1 in case of error
 */
int set_frame_size(int fd, const char *file, int frame_size);

int set_serial_opts(int fd, const char *file);

/* 
 * reset_serial_counters
 * 
 * Resets all the counters to 0
 *
 * Input:
 * - File descriptor
 * - Serial port name
 * Output:
 * - 0 on success and 1 if an error happened
 */
int reset_serial_counters(int fd, const char *file);

/* 
 * update_serial_counters
 * 
 * Calls some ioctl to set the driver counters
 *
 * Input:
 * - File descriptor
 * - Serial port name
 * Output:
 * - 0 on success and 1 if an error happened
 */
int update_serial_counters(int fd, const char *file);
#endif

