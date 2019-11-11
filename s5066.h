/* s5066.h  -  NATO STANAG 5066 Annex A, B, and C definitions
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * See file COPYING.
 *
 * 15.4.2006, created over Easter holiday --Sampo
 */

#ifndef _s5066_h
#define _s5066_h

struct hi_thr;
struct hi_io;
struct hi_pdu;

#include <pthread.h>

#define S5066_SIS  1
#define S5066_DTS  2
#define S5066_SMTP 3
#define S5066_HTTP 4
#define S5066_TEST_PING 5

/* Application SAP IDs. See Annex F. */

#define SAP_ID_SUBNET_MGMT 0
#define SAP_ID_ACKD_MSG    1
#define SAP_ID_UNACKD_MSG  2
#define SAP_ID_HMTP        3
#define SAP_ID_HFPOP       4
#define SAP_ID_OP_OW       5
#define SAP_ID_STREAM      6
#define SAP_ID_DATAGRAM    7
#define SAP_ID_PPP         8
#define SAP_ID_IP          9
/* 10,11 reserved for future assignment */
/* 12,13,14,15 local uses, ad-hoc */
#define SAP_ID_WALKIE_TALKIE 12   /* Half Duplex Digital Voice and chat */
#define SAP_ID_RFID          13   /* RFID tracking application */

/* Clients talking SIS are required to split long application data to several PDUs */
#define SIS_MIN_PDU_SIZE 5     /* preamble, version and length field */
#define SIS_UNIHDR_SIZE 12
#define SIS_MTU 2048           /* Reliable service MTU for u_pdu, see p. A-7 */
#define SIS_BCAST_MTU 4096     /* Maximum size of broadcast MTU for u_pdu, see p. A-7 */
#define SIS_MAX_PDU_SIZE (SIS_MIN_PDU_SIZE + SIS_UNIHDR_SIZE + SIS_BCAST_MTU)

#define DTS_SEG_SIZE 800  /* arbitrarily tunable below 1k (10 bits, see C.3.2.10, p. C-14) */

/* N.B. In practise segment size is limited by 8 bit EOT (End Of Transmission) field
 * that has range of 127.5 seconds. Given slow data rate, a PDU can take long time
 * to transmit. For example: 127 seconds is 1190 bytes @ 75bps or 38KB @ 2400bps.
 * See C.3.2.3, p. C-9 for discussion. */

/* D_PDU type constants */

#define DTS_DATA_ONLY  0
#define DTS_ACK_ONLY   1
#define DTS_DATA_ACK   2
#define DTS_RESET      3
#define DTS_EDATA_ONLY 4
#define DTS_EACK_ONLY  5
#define DTS_MGMT       6
#define DTS_NONARQ     7
#define DTS_ENONARQ    8
/* 9-14 reserved */
#define DTS_WARNING   15

#define SIS_UNIDATA_IND_MIN_HDR 22  /* min == no error and no non-rx'd blocks */

/* S_PDU type constants (these are distinct from primitives) */

#define S_PDU_DATA               0
#define S_PDU_OK                 1
#define S_PDU_FAIL               2
#define S_PDU_HARD_LINK_REQ      3
#define S_PDU_HARD_LINK_OK       4
#define S_PDU_HARD_LINK_REJ      5
#define S_PDU_HARD_LINK_TERM     6
#define S_PDU_HARD_LINK_TERM_OK  7

/* C_PDU type constants (already shifted for use in C_PCI */

#define C_PDU_DATA           0x00
#define C_PDU_LINK_REQ       0x10
#define C_PDU_LINK_OK        0x20
#define C_PDU_LINK_REJ       0x30
#define C_PDU_LINK_BREAK     0x40
#define C_PDU_LINK_BREAK_OK  0x50

void test_ping(struct hi_thr* hit, struct hi_io* io);
int sis_decode(struct hi_thr* hit, struct hi_io* io);
int dts_decode(struct hi_thr* hit, struct hi_io* io);
int smtp_decode_req(struct hi_thr* hit, struct hi_io* io);
int smtp_decode_resp(struct hi_thr* hit, struct hi_io* io);
int http_decode(struct hi_thr* hit, struct hi_io* io);
void dts_send_uni(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int len, char* d);
void sis_send_bind(struct hi_thr* hit, struct hi_io* io, int sap, int rank, int svc_type);
struct hi_pdu* sis_encode_start(struct hi_thr* hit, int op, int len);

struct u_pdu {
  short len;
  char* data;
};

struct sis_sap {
  struct hi_io* io;
  char rank;
  char tx_mode;
  char flags;
  char n_re_tx;
};

#define SIS_MAX_SAP_ID 16
extern struct sis_sap saptab[SIS_MAX_SAP_ID];
extern pthread_mutex_t saptab_mut;

struct c_pdu_buf {
  int size;
  char map[SIS_MAX_PDU_SIZE/8];  /* bitmap of bytes received so we know if we have received all */
  char m[SIS_MAX_PDU_SIZE];
};

struct dts_conn {
  char remote_station_addr[4];
  int c_pdu_id;
  int rx_lwe;
  int rx_uwe;
  char acks[32];  /* Bitmap of acks, kept on receiving end */
  /* *** Do we need "memory ACK" array for misreceived PDUs? */
  struct hi_pdu* nonarq_pdus[4096];  /* The c_pdu_id is 12 bits */
  
  int tx_lwe;
  int tx_uwe;
  struct hi_pdu* tx_pdus[256];  /* Hold PDUs so we can re_tx them if they are not ack'd */
};

/* SMTP support */

#define SMTP_MIN_PDU_SIZE 5     /* preamble and length field */
#define SMTP_MAX_PDU_SIZE 4096

/* States of SMTP parsing for client */
#define SMTP_INIT 0  /* wait for 220 greet\n (server: send 220 greet) */
#define SMTP_EHLO 1  /* 220 greet seen, issue EHLO<sp>s\n */
#define SMTP_RDY  2  /* wait for 250 from EHLO */
#define SMTP_MAIL 3  /*  send piped SMTP payload, MAIL FROM:<a>\n */
#define SMTP_RCPT 4  /*  RCPT TO:<a>\n or DATA\n */
#define SMTP_DATA 5  /*  Data entry stage (354) */
#define SMTP_SEND 6  /* wait for 354, send the message and \r\n.\r\n */
#define SMTP_SENT 7  /* Mail sent (250 after 354) */
#define SMTP_QUIT 8  /* wait for 221 goodbye */

/* States of SMTP parsing for server */
#define SMTP_START  11  /* expect EHLO, send 250 */
#define SMTP_MAIN   12  /* expect MAIL FROM, process RCPT TOs, DATA, and \r\n.\r\n */
#define SMTP_TO     13
#define SMTP_MORE0  14  /* Long mail. Keep sending body as SIS primitives. Noting seen. */
#define SMTP_MORE1  15  /* Assume "\r\n" has been seen and ".\r\n" needs to be seen. */
#define SMTP_MORE2  16  /* Assume "\r\n." has been seen and "\r\n" needs to be seen. */
#define SMTP_WAIT   17  /* expect 354 */
#define SMTP_STATUS 18  /* expect staus of message: 250 = sent, others error */
#define SMTP_END    19  /* expect QUIT and send 221 bye. If get MAIL FROM move to SMTP_MAIN. */

#define SMTP_GREET_DOMAIN "open5066.org"  /* *** config domain */
#define SMTP_EHLO_CLI "Beautiful"

#endif /* _s5066_h */
