/* hiios.h  -  Hiquu I/O Engine
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * This is confidential unpublished proprietary source code of the author.
 * NO WARRANTY, not even implied warranties. Contains trade secrets.
 * Distribution prohibited unless authorized in writing. See file COPYING.
 * $Id: hiios.h,v 1.10 2006/05/24 09:42:11 nito Exp $
 *
 * 15.4.2006, created over Easter holiday --Sampo
 * 23.4.2006, DTS specific enhancements --Sampo
 *
 * See http://pl.atyp.us/content/tech/servers.html for inspiration on threading strategy.
 * http://www.kegel.com/c10k.html
 */

#ifndef _hiios_h
#define _hiios_h

#ifdef LINUX
#include <sys/epoll.h>     /* See man 4 epoll (Linux 2.6) */
#endif
#ifdef SUNOS
#include <sys/devpoll.h>   /* See man -s 7d poll (Solaris 8) */
#include <sys/poll.h>
#endif

#include <netinet/in.h>
#include <sys/uio.h>
#include <pthread.h>
#include "s5066.h"

#ifndef IOV_MAX
#define IOV_MAX 16
#endif
#define HI_N_IOV (IOV_MAX < 32 ? IOV_MAX : 32)   /* Avoid unreasonably huge iov */
#if 1
#define HI_PDU_MEM 2200 /* Default PDU memory buffer size, sufficient for reliable data */
#else
#define HI_PDU_MEM 4200 /* Default PDU memory buffer size, sufficient for broadcast data */
#endif

#define HI_POLL    1    /* Trigger epoll */
#define HI_PDU     2    /* PDU */
#define HI_LISTEN  3    /* Listening socket for TCP */
#define HI_TCP_S   4    /* TCP server socket, i.e. accept(2)'d from listening socket */
#define HI_TCP_C   5    /* TCP client socket, i.e. formed using connect(2) */
#define HI_SNMP    6    /* SNMP (UDP) socket */

struct hi_qel {         /* hiios task que element. This is the first thing on io and pdu objects */
  struct hi_qel* n;     /* Next in todo_queue */
  pthread_mutex_t mut;
  char kind;
  char proto;
  char flags;
  char inqueue;
};

struct hi_io {
  struct hi_qel qel;
  struct hi_io* n;           /* next among io objects, esp. backends */
  struct hi_io* pair;        /* the other half of a proxy connection */
  int fd;
  char *description;         /* Nito: To be able to map fd->devices/ports. Link to hi_host_spec->specstr */
  char events;               /* events from last poll */
  char n_iov;
  struct iovec* iov_cur;     /* not used by listeners, only useful for sessions and backend ses */
  struct iovec iov[HI_N_IOV];
  struct hi_pdu* in_write;   /* list of pdus that are in process of being written (have iovs) */
  int n_to_write;            /* length of to_write queue */
  struct hi_pdu* to_write_consume;  /* list of PDUs that are imminently goint to be written */
  struct hi_pdu* to_write_produce;  /* add new pdus here (main thr only) */
  
  /* Statistics counters */
  int n_written;  /* bytes */
  int n_read;     /* bytes */
  int n_pdu_out;
  int n_pdu_in;
  
  struct hi_pdu* cur_pdu;    /* PDU for which we currently expect to do I/O */
  struct hi_pdu* reqs;       /* linked list of real requests of this session, protect by qel.mut */
  union {
    struct dts_conn* dts;
    int sap;                 /* S5066 SAP ID, indexes into saptab[] and svc_type_tab[] */
    struct {
      struct hi_pdu* uni_ind_hmtp;
      int state;
    } smtp;
  } ad;                      /* Application specific data */
};

struct hi_pdu {
  struct hi_qel qel;
  struct hi_pdu* n;          /* Next among requests or responses */
  struct hi_pdu* wn;         /* Write next. Used by in_write, to_write, and subresps queues. */
  struct hi_io* fe;
  
  struct hi_pdu* req;
  struct hi_pdu* parent;
  
  struct hi_pdu* subresps;   /* subreq: list of resps, to ds_wait() upon */
  struct hi_pdu* reals;      /* linked list of real resps to this req */
  struct hi_pdu* synths;     /* linked list of subreqs and synth resps */

  char events;               /* events needed by this PDU (EPOLLIN or EPOLLOUT) */
  char n_iov;
  struct iovec iov[3];       /* Enough for header, payload, and CRC */
  
  int need;                  /* how much more is needed to complete a PDU? */
  char* scan;                /* How far has protocol parsin progressed, e.g. in SMTP. */
  char* ap;                  /* allocation pointer: next free memory location */
  char* m;                   /* beginning of memory (often m == mem, but could be malloc'd) */
  char* lim;                 /* one past end of memory */
  char mem[HI_PDU_MEM];      /* memory for processing a PDU */

  union {
    struct {
      int n_tx_seq;          /* Transmit Frame Sequence Number */
      int addr_len;
      char* c_pdu;           /* S5066 DTS segmented C_PDU */
    } dts;
    struct {
      char rx_map[SIS_MAX_PDU_SIZE/8];  /* bitmap of bytes rx'd so we know if we have rx'd all */
    } dtsrx;
    struct {
      char* skip_ehlo;
    } smtp;
  } ad;                      /* Application specific data */
  int len;
  int op;
};

struct c_pdu_buf;

struct hiios {
  int ep;       /* epoll(4) (Linux 2.6) or /dev/poll (Solaris 8, man -s 7d poll) file descriptor */
  int n_evs;    /* how many useful events last epoll_wait() returned */
  int max_evs;
#ifdef LINUX
  struct epoll_event* evs;
#endif
#ifdef SUNOS
  struct pollfd* evs;
#endif
  int n_ios;
  int max_ios;
  struct hi_io* ios;

  pthread_mutex_t pdu_mut;
  int max_pdus;
  struct hi_pdu* pdus;  /* Global pool of PDUs */
  struct hi_pdu* free_pdus;

#if 0
  pthread_mutex_t c_pdu_buf_mut;
  int max_c_pdu_bufs;
  struct c_pdu_buf* c_pdu_bufs;       /* global pool for c_pdu buffers */
  struct c_pdu_buf* free_c_pdu_bufs;
#endif

  pthread_mutex_t todo_mut;
  pthread_cond_t todo_cond;
  struct hi_qel* todo_consume;  /* PDUs and I/O objects that need processing. */
  struct hi_qel* todo_produce;
  int n_todo;
  struct hi_qel poll_tok;
};

struct hi_thr {
  struct hiios* shf;
  struct hi_pdu* free_pdus;
  struct c_pdu_buf* free_c_pdu_bufs;
};

struct hi_host_spec {
  struct hi_host_spec* next;
  struct sockaddr_in sin;
  int proto;
  char* specstr;
  struct hi_io* conns;
};

struct hi_proto {
  char name[8];
  int default_port;
  struct hi_host_spec* specs;
};

extern struct hi_proto prototab[];

/* External APIs */

struct hiios* hi_new_shuffler(int nfd, int npdu);
struct hi_io* hi_open_listener(struct hiios* shf, struct hi_host_spec* hs, int proto);
struct hi_io* hi_open_tcp(struct hiios* shf, struct hi_host_spec* hs, int proto);
struct hi_io* hi_add_fd(struct hiios* shf, int fd, int proto, int kind, char *description);

struct hi_pdu* hi_pdu_alloc(struct hi_thr* hit);
void hi_send(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp);
void hi_send1(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp,
	      int len0, char* d0);
void hi_send2(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp,
	      int len0, char* d0, int len1, char* d1);
void hi_send3(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp,
	      int len0, char* d0, int len1, char* d1, int len2, char* d2);
void hi_sendf(struct hi_thr* hit, struct hi_io* io, char* fmt, ...);
void hi_todo_produce(struct hiios* shf, struct hi_qel* qe);
void hi_shuffle(struct hi_thr* hit, struct hiios* shf);

/* Internal APIs */

#define HI_NOERR 0
#define HI_CONN_CLOSE 1

void hi_process(struct hi_thr* hit, struct hi_pdu* pdu);
void hi_in_out( struct hi_thr* hit, struct hi_io* io);
void hi_close(  struct hi_thr* hit, struct hi_io* io);
void hi_write(  struct hi_thr* hit, struct hi_io* io);
void hi_read(   struct hi_thr* hit, struct hi_io* io);

void hi_checkmore(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int minlen);

void hi_free_req(struct hi_thr* hit, struct hi_pdu* pdu);
void hi_free_req_fe(struct hi_thr* hit, struct hi_pdu* req);
void hi_add_to_reqs(struct hi_io* io, struct hi_pdu* req);

#endif /* _hiios_h */
