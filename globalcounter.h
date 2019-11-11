/*
 * $Id: globalcounter.h,v 1.1 2006/05/24 09:42:11 nito Exp $
 *
 * Global counter structure
 *
 * Currently only serial sync is supported
 */

#ifndef GLOBALCOUNTER_H
#define GLOBALCOUNTER_H

union validcountervalues {
  char *string;
  int counter;
};

struct globalcounter {
  union validcountervalues value;
  const char *name;
  const char *description;
};

#define NUM_GLOBALCOUNTERS 20
/*#define NUM_GLOBALCOUNTERS sizeof(globalcounters)/sizeof(struct globalcounter)*/
extern struct globalcounter globalcounters[];

#define SERIAL_IF_WRITE_BYTES 0
#define SERIAL_IF_READ_BYTES 1
#define SERIAL_IF_WRITE_ERRORS 2
#define SERIAL_IF_READ_ERRORS 3
#define SERIAL_IF_MTU 3
#define SERIAL_IF_SPEED 4
#define SERIAL_IF_ADMIN_STATUS 5
#define SERIAL_IF_OPER_STATUS 6
#define SERIAL_IF_ABORT_SIGNALS 7
#define SERIAL_IF_CRC_ERRORS 8
#define SERIAL_IF_CTS_TIMEOUTS 9
#define SERIAL_IF_CARRIER_DROPS 10
#define SERIAL_IF_TX_UNDERRUN 11
#define SERIAL_IF_RCVR_OVERRUN 12
#define SERIAL_IF_RCVR_NO_BUFFERS 13
#define SERIAL_IF_IN_OCTETS 14
#define SERIAL_IF_IN_PKTS 15
#define SERIAL_IF_IN_ERRORS 16
#define SERIAL_IF_OUT_OCTETS 17
#define SERIAL_IF_OUT_PKTS 18
#define SERIAL_IF_OUT_ERRORS 19

/* Size of the globalcounters array */


/* 
 * add_to_globalcounter
 *
 * Adds a value to the global counter
 * Input:
 * - The index of the counter 0..num_globalcounters - 1
 * - The value
 * Output:
 * - The value of the counter or -1 in case that the index
 *   is out of bounds
 */
#define add_to_globalcounter(index,myvalue) \
  (index < 0 || index >= NUM_GLOBALCOUNTERS) ? \
  -1 : \
  (globalcounters[index].value.counter += myvalue)

/* 
 * get_globalcounter
 *
 * Adds a value to the global counter
 * Input:
 * - The index of the counter 0..num_globalcounters - 1
 * Output:
 * - The value of the counter or -1 in case that the index
 *   is out of bounds
 */
#define get_globalcounter(index) \
  ((index < 0) || (index >= NUM_GLOBALCOUNTERS)) ? \
  -1 :						   \
  globalcounters[index].value.counter


/* 
 * set_globalcounter
 *
 * sets the value of the global counter
 * Input:
 * - The index of the counter 0..num_globalcounters - 1
 * - The value
 * Output:
 * - The value of the counter or -1 in case that the index
 *   is out of bounds
 */
#define set_globalcounter(index,myvalue) \
  (index < 0 || index >= NUM_GLOBALCOUNTERS) ? \
  -1 :	\
  (globalcounters[index].value.counter=myvalue)



/* 
 * reset_globalcounter
 *
 * sets the value of the global counter to 0
 * Input:
 * - The index of the counter 0..num_globalcounters - 1
 * Output:
 * - The value of the counter or -1 in case that the index
 *   is out of bounds
 */
#define reset_globalcounter(index) \
  set_globalcounter(index,0)


/* 
 * reset_allglobalcounter
 *
 * sets the value of the all the global counters to 0
 * Input:
 * - None
 * Output:
 * - None
 */
int reset_allglobalcounters();


/* 
 * get_globalcounter_name
 *
 * Returns the name of the global counter
 * Input:
 * - The index of the counter 0..num_globalcounters - 1
 * Output:
 * - The name of the counter or NULL in case that the index
 *   is out of bounds
 */
#define get_globalcounter_name(index) \
  (index < 0 || index >= NUM_GLOBALCOUNTERS) ? \
  NULL:					       \
  globalcounters[index].name


/* 
 * get_globalcounter_description
 *
 * Returns the description of the global counter
 * Input:
 * - The index of the counter 0..num_globalcounters - 1
 * Output:
 * - The name of the counter or NULL in case that the index
 *   is out of bounds
 */
#define get_globalcounter_description(index) \
  (index < 0 || index >= NUM_GLOBALCOUNTERS) ? \
  NULL:					       \
  globalcounters[index].description 

#endif
