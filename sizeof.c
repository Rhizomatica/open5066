/* sizeof.c  -  Print sizes of various data structures
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * See file COPYING.
 *
 * 15.4.2006, created over Easter holiday --Sampo
 */

#include <signal.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "errmac.h"
#include "hiios.h"
#include "afr.h"
#include "s5066.h"
#include "sis5066.h"

#define PRINT_SIZE(x) printf("%5d bytes sizeof(" #x ")\n", sizeof(x))
#define PRINT_SIZE_MAX(x) printf("%5d bytes sizeof(" #x ")\n", sizeof(x)); if (sizeof(x)>m) m = sizeof(x)

int main(int argc, char** argv, char** env)
{
  struct s_hdr* fp;
  struct s_hdr* xp;
  struct s_hdr x;
  PRINT_SIZE(struct hi_qel);
  PRINT_SIZE(struct hi_io);
  PRINT_SIZE(struct hi_pdu);
  PRINT_SIZE(struct hiios);
  PRINT_SIZE(struct hi_thr);
  PRINT_SIZE(struct hi_host_spec);

  printf("\nsis5066.h\n\n");

PRINT_SIZE(struct service_type);
PRINT_SIZE(struct bind_request);
PRINT_SIZE(struct unbind_request);
PRINT_SIZE(struct bind_accepted);
PRINT_SIZE(struct bind_rejected);
PRINT_SIZE(struct unbind_indication);
PRINT_SIZE(struct hard_link_establish);
PRINT_SIZE(struct hard_link_terminate);
PRINT_SIZE(struct hard_link_established);
PRINT_SIZE(struct hard_link_rejected);
PRINT_SIZE(struct hard_link_terminated);
PRINT_SIZE(struct hard_link_indication);
PRINT_SIZE(struct hard_link_accept);
PRINT_SIZE(struct hard_link_reject);
PRINT_SIZE(struct subnet_availability);
PRINT_SIZE(struct data_flow_on);
PRINT_SIZE(struct data_flow_off);
PRINT_SIZE(struct keep_alive);
PRINT_SIZE(struct management_message_request);
PRINT_SIZE(struct management_message_indication);
PRINT_SIZE(struct delivery_mode_field);
PRINT_SIZE(struct unidata_req);
PRINT_SIZE(struct unidata_ind);
PRINT_SIZE(struct unidata_ind_non_arq);
PRINT_SIZE(struct unidata_req_confirm);
PRINT_SIZE(struct unidata_req_rejected);

PRINT_SIZE(struct expedited_unidata_req);
PRINT_SIZE(struct expedited_unidata_ind);
PRINT_SIZE(struct expedited_unidata_req_confirm);
PRINT_SIZE(struct expedited_unidata_req_rejected);
PRINT_SIZE(struct naked_s_hdr);
PRINT_SIZE(struct s_hdr);

  printf("\nSPRIM_TLEN(bind_request)=%d\n", SPRIM_TLEN(bind_request));

  fp = &x.sprim.unidata_ind.size_of_u_pdu;
  xp = &x;
  printf("addr_of(sprim.unidata_ind.size_of_u_pdu)=%d\n", (int)fp - (int)xp);
  return 0;
}

/* EOF  --  sizeof.c */
