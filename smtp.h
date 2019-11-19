/* smtp.h  -  Simple Mail Transfer Protocol, RFC822
 *
 * 19.11.2019, Added header -- Rafael Diniz <rafael@rhizomatica.org>
 */

#ifndef _smtp_h
#define _smtp_h

#include "hiios.h"

// used by sis.c
void smtp_send(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int len, char* d);

#endif /* _smtp_h */
