/* hiwrite.c  -  Hiquu I/O Engine Write Operation.
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * See file COPYING.
 *
 * 15.4.2006, created over Easter holiday --Sampo
 * 22.4.2006, refined multi iov sends over the weekend --Sampo
 *
 * Idea: Consider separate lock for maintenance of to_write queue and separate
 * for in_write, iov, and actual wrintev().
 */

#include <pthread.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "afr.h"
#include "hiios.h"
#include "errmac.h"

void hi_send0(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp)
{
  if (req) {
    resp->req = req;
    resp->n = req->reals;
    req->reals = resp;
  } else {
    resp->req = resp->n = 0;
  }
  
  LOCK(io->qel.mut, "");
  if (!io->to_write_produce)
    io->to_write_consume = resp;
  else
    io->to_write_produce->qel.n = &resp->qel;
  io->to_write_produce = resp;
  resp->qel.n = 0;
  ++io->n_to_write;
  ++io->n_pdu_out;
  UNLOCK(io->qel.mut, "");
  
  D("hisend pdu(%p) fd(%x)", resp, io->fd);
  hi_write(hit, io);   /* Try cranking the write machine right away! */
  /*hi_todo_produce(hit->shf, &io->qel);*/
}

void hi_send(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp)
{
  hi_send1(hit, io, req, resp, resp->len, resp->m);
}

void hi_send1(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp,
	      int len0, char* d0)
{
  resp->n_iov = 1;
  resp->iov[0].iov_len = len0;
  resp->iov[0].iov_base = d0;
  HEXDUMP("iov0: ", d0, d0+len0, 800);
  hi_send0(hit, io, req, resp);
}

void hi_send2(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp,
	      int len0, char* d0, int len1, char* d1)
{
  resp->n_iov = 2;
  resp->iov[0].iov_len  = len0;
  resp->iov[0].iov_base = d0;
  resp->iov[1].iov_len  = len1;
  resp->iov[1].iov_base = d1;
  HEXDUMP("iov0: ", d0, d0+len0, 800);
  HEXDUMP("iov1: ", d1, d1+len1, 800);
  hi_send0(hit, io, req, resp);
}

void hi_send3(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp,
	      int len0, char* d0, int len1, char* d1, int len2, char* d2)
{
  resp->n_iov = 3;
  resp->iov[0].iov_len  = len0;
  resp->iov[0].iov_base = d0;
  resp->iov[1].iov_len  = len1;
  resp->iov[1].iov_base = d1;
  resp->iov[2].iov_len  = len2;
  resp->iov[2].iov_base = d2;
  HEXDUMP("iov0: ", d0, d0+len0, 800);
  HEXDUMP("iov1: ", d1, d1+len1, 800);
  HEXDUMP("iov2: ", d2, d2+len2, 800);
  hi_send0(hit, io, req, resp);
}

void hi_sendf(struct hi_thr* hit, struct hi_io* io, char* fmt, ...)
{
  va_list pv;
  struct hi_pdu* pdu = hi_pdu_alloc(hit);
  if (!pdu) { ERR("Out of PDUs in bad place fmt(%s)", fmt); return; }
  
  va_start(pv, fmt);
  pdu->len = vsnprintf(pdu->m, pdu->lim - pdu->m, fmt, pv);
  va_end(pv);
  
  pdu->ap += pdu->len;
  hi_send(hit, io, 0, pdu);
}

static void hi_make_iov(struct hi_io* io)
{
  struct hi_pdu* pdu;
  struct iovec* lim = io->iov+HI_N_IOV;
  struct iovec* cur = io->iov_cur = io->iov;
  LOCK(io->qel.mut, "");
  while ((pdu = io->to_write_consume) && (cur + pdu->n_iov) <= lim) {
    memcpy(cur, pdu->iov, pdu->n_iov * sizeof(struct iovec));
    cur += pdu->n_iov;
    
    if (!(io->to_write_consume = pdu->wn))    /* consume from to_write */
      io->to_write_produce = 0;
    --io->n_to_write;
    pdu->wn = io->in_write;                   /* produce to in_write */
    io->in_write = pdu;
    
    ASSERT(io->n_to_write >= 0);
    ASSERT(pdu->n_iov && pdu->iov[0].iov_len);   /* Empty writes can lead to infinite loops */
  }
  UNLOCK(io->qel.mut, "");
  io->n_iov = cur - io->iov_cur;
}

/* *** Here complex determination about freeability of a PDU needs to be done.
 * For now we "fake" it by assuming that a response sufficies to free request.
 * In real life you would have to consider
 * a. multiple responses
 * b. subrequests and their responses
 * c. possibility of sending a response before processing of request itself has ended
 */

void hi_free_resp(struct hi_thr* hit, struct hi_pdu* resp)
{
  struct hi_pdu* pdu = resp->req->reals;
  
  /* Remove resp from request's real response list. resp MUST be in this list: if it
   * is not, pdu-n (next) pointer chasing will lead to NULL dereference (by design). */
  
  if (resp == pdu)
    resp->req->reals = pdu->n;
  else
    for (; 1; pdu = pdu->n)
      if (pdu->n == resp) {
	pdu->n = resp->n;
	break;
      }
  
  resp->qel.n = (struct hi_qel*)(hit->free_pdus);         /* move to free list */
  hit->free_pdus = resp;
  D("resp(%p) freed", resp);
}

/* May be called either because individual resp was done, or because of connection close. */

void hi_free_req(struct hi_thr* hit, struct hi_pdu* req)
{
  struct hi_pdu* pdu;
  
  for (pdu = req->reals; pdu; pdu = pdu->n) { /* free dependent resps */
    pdu->qel.n = &hit->free_pdus->qel;
    hit->free_pdus = pdu;
  }
  
  req->qel.n = (struct hi_qel*)(hit->free_pdus);          /* move to free list */
  hit->free_pdus = req;
  D("req(%p) freed", req);
}

void hi_free_req_fe(struct hi_thr* hit, struct hi_pdu* req)
{
  struct hi_pdu* pdu;
  hi_free_req(hit, req);
  ASSERT(req->fe);
  if (!req->fe)
    return;
  
  /* Scan the frontend to find the reference. The theory is that
   * hi_free_req_fe() only gets called when its known that the request is in the queue.
   * If it is not, the loop will run off the end and crash with NULL pointer. */
  
  LOCK(req->fe->qel.mut, "del from reqs");
  pdu = req->fe->reqs;
  if (pdu == req)
    req->fe->reqs = req->n;
  else
    for (; 1; pdu = pdu->n)
      if (pdu->n == req) {
	pdu->n = req->n;
	break;
      }
  UNLOCK(req->fe->qel.mut, "del from reqs");
}

/* Often moving PDU to reqs means it should stop being cur_pdu. This is either
 * handeld by explicit manipulation of io->cur_pdu or by calling hi_checkmore() */

void hi_add_to_reqs(struct hi_io* io, struct hi_pdu* req)
{
  LOCK(io->qel.mut, "add to reqs");
  req->fe = io;
  req->n = io->reqs;
  io->reqs = req;
  UNLOCK(io->qel.mut, "add to reqs");
}

static void hi_clear_iov(struct hi_thr* hit, struct hi_io* io, int n)
{
  struct hi_pdu* pdu;
  io->n_written += n;
  while (io->n_iov && n) {
    if (n >= io->iov_cur->iov_len) {
      n -= io->iov_cur->iov_len;
      ++io->iov_cur;
      --io->n_iov;
      ASSERTOP(io->iov_cur, >=, 0);
    } else {
      /* partial write: need to adjust iov_cur->iov_base */
      io->iov_cur->iov_base = ((char*)(io->iov_cur->iov_base)) + n;
      io->iov_cur->iov_len -= n;
      return;  /* we are not in end so nothing to free */
    }
  }
  ASSERTOP(n, ==, 0);
  if (io->n_iov)
    return;
  
  /* Everything has now been written. Time to free in_write list. */
  
  D("freeing responses (and possibly requests) %d", 0);
  while ((pdu = io->in_write)) {
    io->in_write = pdu->wn;
    pdu->wn = 0;
    
    if (!pdu->req) continue;
    
    /* Only a response can cause anything freed, and every response is freeable upon write. */
    
    hi_free_resp(hit, pdu);
    if (!pdu->req->reals)
      hi_free_req(hit, pdu->req);  /* last response, free the request */
  }
}

/* This function can only be called by one thread at a time because the todo_queue
 * only admits an io object once and only one thread can consume it. Thus locking
 * is really needed only to protect the to_write queue, see hi_make_iov(). */

void hi_write(struct hi_thr* hit, struct hi_io* io)
{
  int ret;
  while (1) {   /* Write until exhausted! */
    if (!io->in_write)  /* Need to prepare new iov? */
      hi_make_iov(io);
    if (!io->in_write)
      return;            /* Nothing further to write */
  retry:
    D("writev(%x) n_iov=%d", io->fd, io->n_iov);
    ret = writev(io->fd, io->iov_cur, io->n_iov);
    switch (ret) {
    case 0: NEVERNEVER("writev on %x returned 0", io->fd);
    case -1:
      switch (errno) {
      case EINTR:  goto retry;
      case EAGAIN: return;  /* writev(2) exhausted (c.f. edge triggered epoll) */
      default:
	ERR("writev(%x) failed: %d %s (closing connection)", io->fd, errno, STRERROR(errno));
	UNLOCK(io->qel.mut, "closefd");
	hi_close(hit, io);
	return;
      }
    default:  /* something was written, deduce it from the iov */
      D("wrote(%x) %d bytes", io->fd, ret);
      hi_clear_iov(hit, io, ret);
    }
  }
}

/* EOF  --  hiwrite.c */
