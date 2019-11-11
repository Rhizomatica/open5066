/* hiread.c  -  Hiquu I/O Engine Read Operations
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * See file COPYING.
 *
 * 15.4.2006, created over Easter holiday --Sampo
 *
 * Read more data to existing PDU (cur_pdu), or create new PDU and read data into it.
 */

#include <pthread.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include "afr.h"
#include "hiios.h"
#include "errmac.h"
#include "s5066.h"

struct hi_pdu* hi_pdu_alloc(struct hi_thr* hit)
{
  struct hi_pdu* pdu;
  if (hit->free_pdus) {
    pdu = hit->free_pdus;
    hit->free_pdus = (struct hi_pdu*)pdu->qel.n;
    D("alloc pdu(%p) from thread", pdu);
    goto retpdu;
  }

  LOCK(hit->shf->pdu_mut, "pdu_alloc");
  if (hit->shf->free_pdus) {
    pdu = hit->shf->free_pdus;
    hit->free_pdus = (struct hi_pdu*)pdu->qel.n;
    UNLOCK(hit->shf->pdu_mut, "pdu_alloc ok");
    D("alloc pdu(%p) from shuffler", pdu);
    goto retpdu;
  }
  UNLOCK(hit->shf->pdu_mut, "pdu_alloc no pdu");
  
  D("out of pdus %d", 0);
  return 0;

 retpdu:
  pdu->lim = pdu->mem + HI_PDU_MEM;
  pdu->m = pdu->scan = pdu->ap = pdu->mem;
  pdu->req = pdu->parent = pdu->subresps = pdu->reals = pdu->synths = 0;
  pdu->fe = 0;
  pdu->need = 1;  /* trigger network I/O */
  pdu->n = 0;
  return pdu;
}

/* As hi_checkmore() will cause cur_pdu to change, it is common to call hi_add_reqs() */

void hi_checkmore(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int minlen)
{
  int n = req->ap - req->m;
  ASSERT(minlen);  /* If this is ever zero it will prevent hi_poll() from producing. */
  if (n > req->len) {
    struct hi_pdu* nreq = hi_pdu_alloc(hit);
    if (!nreq) { NEVERNEVER("*** out of pdus in bad place %d", n); }
    nreq->need = minlen;
    memcpy(nreq->ap, req->m + req->len, n - req->len);
    nreq->ap += n - req->len;
    io->cur_pdu = nreq;
    req->ap = req->m + req->len;
  } else
    io->cur_pdu = 0;
}

void hi_read(struct hi_thr* hit, struct hi_io* io)
{
  int ret;
  while (1) {  /* eagerly read until we exhaust the read (c.f. edge triggered epoll) */
    if (!io->cur_pdu) {  /* need to create a new PDU */
      io->cur_pdu = hi_pdu_alloc(hit);
      if (!io->cur_pdu) {
	/* Allocation failure, retry later. Back to todo because we did not exhaust read */
	hi_todo_produce(hit->shf, &io->qel);
	return;
      }
      ++io->n_pdu_in;
      /* set fe? */
    }
  retry:
    D("read(%x)", io->fd);
    ret = read(io->fd, io->cur_pdu->ap, io->cur_pdu->lim - io->cur_pdu->ap); /* *** vs. need */
    switch (ret) {
    case 0:
      /* *** any provision to process still pending PDUs? */
      hi_close(hit, io);
      return;
    case -1:
      switch (errno) {
      case EINTR:  goto retry;
      case EAGAIN: return;  /* read(2) exhausted (c.f. edge triggered epoll) */
      default:
	ERR("read(%x) failed: %d %s (closing connection)", io->fd, 
	    errno, 
	    STRERROR(errno));
	hi_close(hit, io);
	return;
      }
    default:  /* something was read, invoke PDU parsing layer */
      D("got(%x) %d bytes, proto=%d cur_pdu(%p) need=%d", io->fd, ret, io->qel.proto, io->cur_pdu, io->cur_pdu->need);
      HEXDUMP("got: ", io->cur_pdu->ap, io->cur_pdu->ap + ret, 800);
      io->cur_pdu->ap += ret;
      io->n_read += ret;
      while (io->cur_pdu
	     && io->cur_pdu->need   /* no further I/O desired */
	     && io->cur_pdu->need <= (io->cur_pdu->ap - io->cur_pdu->m)) {
	switch (io->qel.proto) {
	  /* *** add here a project dependent include? Or a macro. */
	  /* Following decoders MUST
	   * a. drop connection in which case any rescheduling is moot
	   * b. consume cur_pdu (set it to 0 or new PDU). This will cause
	   *    read to be consumed until exhausted, and later trigger new todo
	   *    when there is more data to be had, see hi_poll()
	   * c. take some other action such as scheduling PDU to todo. Typically
	   *    the req->need is zero when I/O is not expected.	   */
	case S5066_SIS:   if (sis_decode(hit, io))   goto conn_close;  break;
	case S5066_DTS:	  if (dts_decode(hit, io))   goto conn_close;  break;
	case S5066_HTTP:  if (http_decode(hit, io))  goto conn_close;  break;
	case S5066_TEST_PING: test_ping(hit, io);  break;
	case S5066_SMTP:
	  if (io->qel.kind == HI_TCP_C) {
	    if (smtp_decode_resp(hit, io))  return;
	  } else {
	    if (smtp_decode_req(hit, io))   return;
	  }
	  break;
	default: NEVERNEVER("unknown proto(%x)", io->qel.proto);
	}
      }
    }
  }
 conn_close:
  hi_close(hit, io);
}

/* EOF  --  hiread.c */
