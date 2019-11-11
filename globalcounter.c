/*
 * $Id: globalcounter.c,v 1.1 2006/05/24 09:42:10 nito Exp $
 *
 * Global counter structure
 *
 * Currently only serial sync is supported
 */
#include <unistd.h>
#include "globalcounter.h"
struct globalcounter globalcounters[] = {
  { 0, 
    "serialIfWriteBytes", 
    "Number of bytes transmitted over the serial interface (from the write syscall)."
  },
  { 0, 
    "serialIfReadBytes", 
    "Number of bytes Received over the serial interface (from the read syscall)."
  },
  { 0, 
    "serialIfWriteErrors", 
    "Number of Errors during trying to transmit over the serial interface (from the write syscall)."
  },
  { 0, 
    "serialIfReadBytes", 
    "Number of bytes Received over the serial interface (from the read syscall)."
  },
  { 0, 
    "serialIfMtu", 
    "The size of the largest packet which can be sent/received on the interface, specified in octets. Only makes sense in sync interfaces."
  },
  { 0, 
    "serialIfSpeed", 
    "An estimate of the interface's current bandwidth in bauds, this object should contain the nominal bandwidth."
  },
  { 0, 
    "serialIfAdminStatus", 
    "The desired state of the interface. The testing(3) state indicates that no operational packets can be passed."
  },
  { 0, 
    "serialIfOperStatus", 
    "The current operational state of the interface. The testing(3) state indicates that no operational packets can be passed."
  },
  { 0, 
    "serialIfAbortSignals", 
    "The number of abort signals received on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialCRCErrors", 
    "The number of CRC errors received on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialCTSTimeouts", 
    "The number of CTS timeouts received on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialCarrierDrops", 
    "The number of Carrier drops on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialTxUnderrun", 
    "The number of Transmitter underrun on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialRcvrOverrun", 
    "The number of Receiver overrun on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialRcvrNoBuffers", 
    "The number of no active receive blocks available. This info is from the serial driver."
  },
  { 0, 
    "serialIfInOctets", 
    "The number incoming bytes on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialIfInPkts", 
    "The number incoming frames on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialIfInErrors", 
    "The number incoming input errors on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialIfOutOctets", 
    "The number outgoing bytes on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialIfOutPkts", 
    "The number outgoing frames on the serial interface. This info is from the serial driver."
  },
  { 0, 
    "serialIfOutErrors", 
    "The number outgoing errors on the serial interface. This info is from the serial driver."
  }
};

int reset_allglobalcounters() {
  int i; 

  for (i=0; i <NUM_GLOBALCOUNTERS; ++i) {
    reset_globalcounter(i);
  }
}

