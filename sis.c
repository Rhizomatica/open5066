/* sis.c  -  NATO STANAG 5066 Annex A Processing
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * This is confidential unpublished proprietary source code of the author.
 * NO WARRANTY, not even implied warranties. Contains trade secrets.
 * Distribution prohibited unless authorized in writing. See file COPYING.
 * $Id: sis.c,v 1.10 2006/05/16 22:17:01 sampo Exp $
 *
 * 15.4.2006, started work over Easter holiday --Sampo
 * 22.4.2006, complete unidata sending over the weekend --Sampo
 */

#include "afr.h"
#include "hiios.h"
#include "errmac.h"
#include "s5066.h"
#include "sis5066.h"    /* from libnc3a, see COPYING_sis5066_h */

#include <ctype.h>
#include <memory.h>
#include <netinet/in.h> /* htons(3) and friends */

/* ================== SENDING SIS PRIMITIVES ================== */

struct sis_sap saptab[SIS_MAX_SAP_ID];
pthread_mutex_t saptab_mut = MUTEX_INITIALIZER;
int sismtu = 200;  /* *** how to determine correct value? */
int sisconfirm_max = 100; /* Maximum amount of confirmation PDU data */
int sislocalconfirmhack = 1; /* fakes node delivery and client delivery confirmations by
				confirming before even sending data to DTS */

struct hi_pdu* sis_encode_start(struct hi_thr* hit, int op, int len)
{
  struct hi_pdu* resp = hi_pdu_alloc(hit);
  if (!resp) { NEVERNEVER("*** out of pdus in bad place %d", op); }
  resp->len = len;
  resp->ap += len;
  len -= 5;  /* exclude preamble and length field from primitive length */
  resp->m[0] = 0x90;
  resp->m[1] = 0xeb;
  resp->m[2] = 0x00;
  resp->m[3] = (len >> 8) & 0x00ff;
  resp->m[4] = len & 0x00ff;
  resp->m[5] = op;
  return resp;
}

void sis_send_bind(struct hi_thr* hit, struct hi_io* io, int sap, int rank, int svc_type)
{
  struct hi_pdu* resp = sis_encode_start(hit, S_BIND_REQUEST, SPRIM_TLEN(bind_request));
  resp->m[6] = (sap << 4) & 0xf0 |  rank & 0x0f;
  resp->m[7] = (svc_type >> 4) & 0x00ff;
  resp->m[8] = (svc_type << 4) & 0xf0;
  hi_send(hit, io, 0, resp);
}

void sis_send_bind_rej(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int reason)
{
  struct hi_pdu* resp = sis_encode_start(hit, S_BIND_REJECTED, SPRIM_TLEN(bind_rejected));
  resp->m[6] = reason;
  hi_send(hit, io, req, resp);
}

void sis_send_bind_ok(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int sap, int mtu)
{
  struct hi_pdu* resp = sis_encode_start(hit, S_BIND_ACCEPTED, SPRIM_TLEN(bind_accepted));
  resp->m[6] = (sap << 4) & 0xf0;
  resp->m[7] = (mtu >> 8) & 0xff;
  resp->m[8] = mtu & 0xff;
  hi_send(hit, io, req, resp);
}

void sis_send_unbind_ind(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int res)
{
  struct hi_pdu* resp = sis_encode_start(hit, S_UNBIND_INDICATION, SPRIM_TLEN(unbind_indication));
  resp->m[6] = res;
  hi_send(hit, io, req, resp);
}

void sis_send_uni_ok(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req)
{
  int size = MIN(sisconfirm_max, ntohs(((struct s_hdr*)req->m)->sprim.unidata_req.size_of_pdu));
  int len  = SPRIM_TLEN(unidata_req_confirm);
  struct hi_pdu* resp = sis_encode_start(hit, S_UNIDATA_REQUEST_CONFIRM, len + size);
  ((struct s_hdr*)resp->m)->sprim.unidata_req_confirm.not_used = 0;
  ((struct s_hdr*)resp->m)->sprim.unidata_req_confirm.dest_sap_id = ((struct s_hdr*)req->m)->sprim.unidata_req.sap_id;
  memcpy(((struct s_hdr*)resp->m)->sprim.unidata_req_confirm.dest_node,
	 ((struct s_hdr*)req->m)->sprim.unidata_req.addr_fld, 4);
  ((struct s_hdr*)resp->m)->sprim.unidata_req_confirm.size_of_u_pdu = htons(size);
  hi_send2(hit, io, req, resp, len, resp->m, size, req->m + len);
}

/* ================== DECODING SIS PRIMITIVES ================== */

void sis_clean(struct hi_io* io)
{
  int i;
  LOCK(saptab_mut, "clean");
  for (i = 0; i < SIS_MAX_SAP_ID; ++i)
    if (saptab[i].io == io)
      saptab[i].io = 0;
  UNLOCK(saptab_mut, "clean");
}

#define SIS_LEN_CHECK(req, strct) MB if ((req)->len != SPRIM_TLEN(strct)) { \
    ERR("Bad SIS PDU. fd(%x) op(%x) len(%d) not sizeof(" #strct ")=%d", \
	(req)->fe->fd, (req)->op, (req)->len, SPRIM_TLEN(strct)); \
    return HI_CONN_CLOSE; } ME
#define SIS_LEN_CHECK2(req, strct) MB if ((req)->len < SPRIM_TLEN(strct)) { \
    ERR("Bad SIS PDU. fd(%x) op(%x) len(%d) not >= sizeof(" #strct ")=%d", \
	(req)->fe->fd, (req)->op, (req)->len, SPRIM_TLEN(strct)); \
    return HI_CONN_CLOSE; } ME

static int sis_bind(struct hi_thr* hit, struct hi_pdu* req)
{
  int sap, mtu;
  SIS_LEN_CHECK(req, bind_request);
  sap = ((struct s_hdr*)req->m)->sprim.bind_request.sap_id;
  LOCK(saptab_mut, "bind");
  if (saptab[sap].io) {
    UNLOCK(saptab_mut, "bind rej");
    D("Rejecting bind fd(%x)", req->fe->fd);
    sis_send_bind_rej(hit, req->fe, req, SAP_ALRDY_ALLOC);
  }
  saptab[sap].io = req->fe; /* grab a slot */
  saptab[sap].rank    = ((struct s_hdr*)req->m)->sprim.bind_request.rank;
  saptab[sap].tx_mode = ((struct s_hdr*)req->m)->sprim.bind_request.service_type.tx_mode;
  saptab[sap].n_re_tx = ((struct s_hdr*)req->m)->sprim.bind_request.service_type.no_retxs;
  saptab[sap].flags
    = ((struct s_hdr*)req->m)->sprim.bind_request.service_type.dlvry_cnfrm << 2
    | ((struct s_hdr*)req->m)->sprim.bind_request.service_type.dlvry_ordr << 1
    | ((struct s_hdr*)req->m)->sprim.bind_request.service_type.ext_fld
    ;
  mtu = sismtu;
  UNLOCK(saptab_mut, "bind ok");
  
  D("bind accepted sap(%d) req(%p)", sap, req);
  req->fe->ad.sap = sap;
  sis_send_bind_ok(hit, req->fe, req, sap, mtu);
  return 0;
}

static int sis_unbind(struct hi_thr* hit, struct hi_pdu* req)
{
  SIS_LEN_CHECK(req, unbind_request);
  sis_clean(req->fe);
  D("unbind req(%p)", req);
  sis_send_unbind_ind(hit, req->fe, req, 0);
  return 0;
}

static int sis_bind_ok(struct hi_thr* hit, struct hi_pdu* req)
{
  int sap, mtu;
  SIS_LEN_CHECK(req, bind_accepted);
  sap = ((struct s_hdr*)req->m)->sprim.bind_accepted.sap_id;
  mtu = ((struct s_hdr*)req->m)->sprim.bind_accepted.mtu;
  D("bind accepted(%p) sap=%d mtu=%d", req, sap, mtu);
  /* *** on client side, no further action is needed. Just ignore PDU. */
  return 0;
}

static int sis_bind_rej(struct hi_thr* hit, struct hi_pdu* req)
{
  int reason;
  SIS_LEN_CHECK(req, bind_rejected);
  reason = ((struct s_hdr*)req->m)->sprim.bind_rejected.reason;
  D("bind rejected(%p) reason=%x", req, reason);
  /* *** on client side, this should probably cause connection drop */
  return 0;
}

static int sis_unbind_ind(struct hi_thr* hit, struct hi_pdu* req)
{
  int reason;
  SIS_LEN_CHECK(req, bind_rejected);
  reason = ((struct s_hdr*)req->m)->sprim.unbind_indication.reason;
  D("unbind indication(%p) reason=%x", req, reason);
  /* *** on client side, this should probably cause connection drop */
  return 0;
}

static int sis_hle(struct hi_thr* hit, struct hi_pdu* req)
{
  int link_type, prio, sap;
  SIS_LEN_CHECK(req, hard_link_establish);
  link_type = ((struct s_hdr*)req->m)->sprim.hard_link_establish.link_type;
  prio = ((struct s_hdr*)req->m)->sprim.hard_link_establish.link_priority;
  sap = ((struct s_hdr*)req->m)->sprim.hard_link_establish.remote_sap_id;
  /* *** decode address field */
  D("hard link establish(%p) ltype=%x prio=%x sap=%x", req, link_type, prio, sap);
  /* *** next action? */
  return 0;
}

static int sis_hlt(struct hi_thr* hit, struct hi_pdu* req)
{
  SIS_LEN_CHECK(req, hard_link_terminate);
  /* *** decode address field */
  D("hard link terminate(%p)", req);
  /* *** next action? */
  return 0;
}

static int sis_hl_ok(struct hi_thr* hit, struct hi_pdu* req)
{
  int status, link_type, prio, sap;
  SIS_LEN_CHECK(req, hard_link_established);
  status = ((struct s_hdr*)req->m)->sprim.hard_link_established.remote_node_status;
  link_type = ((struct s_hdr*)req->m)->sprim.hard_link_established.link_type;
  prio = ((struct s_hdr*)req->m)->sprim.hard_link_established.link_priority;
  sap = ((struct s_hdr*)req->m)->sprim.hard_link_established.remote_sap_id;
  /* *** decode address field */
  D("hard link established(%p) remote_status=%x ltype=%x prio=%x sap=%x", req, status, link_type, prio, sap);
  /* *** next action? */
  return 0;
}

static int sis_hl_rej(struct hi_thr* hit, struct hi_pdu* req)
{
  int reason, link_type, prio, sap;
  SIS_LEN_CHECK(req, hard_link_rejected);
  reason = ((struct s_hdr*)req->m)->sprim.hard_link_rejected.reason;
  link_type = ((struct s_hdr*)req->m)->sprim.hard_link_rejected.link_type;
  prio = ((struct s_hdr*)req->m)->sprim.hard_link_rejected.link_priority;
  sap = ((struct s_hdr*)req->m)->sprim.hard_link_rejected.remote_sap_id;
  /* *** decode address field */
  D("hard link established(%p) reason=%x ltype=%x prio=%x sap=%x", req, reason, link_type, prio, sap);
  /* *** next action? */
  return 0;
}

static int sis_hlt_ok(struct hi_thr* hit, struct hi_pdu* req)
{
  int reason, link_type, prio, sap;
  SIS_LEN_CHECK(req, hard_link_terminated);
  reason = ((struct s_hdr*)req->m)->sprim.hard_link_terminated.reason;
  link_type = ((struct s_hdr*)req->m)->sprim.hard_link_terminated.link_type;
  prio = ((struct s_hdr*)req->m)->sprim.hard_link_terminated.link_priority;
  sap = ((struct s_hdr*)req->m)->sprim.hard_link_terminated.remote_sap_id;
  /* *** decode address field */
  D("hard link terminated(%p) reason=%x ltype=%x prio=%x sap=%x", req, reason,link_type,prio,sap);
  /* *** next action? */
  return 0;
}

static int sis_hl_ind(struct hi_thr* hit, struct hi_pdu* req)
{
  int status, link_type, prio, sap;
  SIS_LEN_CHECK(req, hard_link_indication);
  status = ((struct s_hdr*)req->m)->sprim.hard_link_indication.remote_node_status;
  link_type = ((struct s_hdr*)req->m)->sprim.hard_link_indication.link_type;
  prio = ((struct s_hdr*)req->m)->sprim.hard_link_indication.link_priority;
  sap = ((struct s_hdr*)req->m)->sprim.hard_link_indication.remote_sap_id;
  /* *** decode address field */
  D("hard link indication(%p) remote_status=%x ltype=%x prio=%x sap=%x", req, status, link_type, prio, sap);
  /* *** next action? */
  return 0;
}

static int sis_hla(struct hi_thr* hit, struct hi_pdu* req)
{
  int link_type, prio, sap;
  SIS_LEN_CHECK(req, hard_link_accept);
  link_type = ((struct s_hdr*)req->m)->sprim.hard_link_accept.link_type;
  prio = ((struct s_hdr*)req->m)->sprim.hard_link_accept.link_priority;
  sap = ((struct s_hdr*)req->m)->sprim.hard_link_accept.remote_sap_id;
  /* *** decode address field */
  D("hard link accept(%p) ltype=%x prio=%x sap=%x", req, link_type, prio, sap);
  /* *** next action? */
  return 0;
}

static int sis_hlr(struct hi_thr* hit, struct hi_pdu* req)
{
  int reason, link_type, prio, sap;
  SIS_LEN_CHECK(req, hard_link_reject);
  reason = ((struct s_hdr*)req->m)->sprim.hard_link_reject.reason;
  link_type = ((struct s_hdr*)req->m)->sprim.hard_link_reject.link_type;
  prio = ((struct s_hdr*)req->m)->sprim.hard_link_reject.link_priority;
  sap = ((struct s_hdr*)req->m)->sprim.hard_link_reject.remote_sap_id;
  /* *** decode address field */
  D("hard link reject(%p) reason=%x ltype=%x prio=%x sap=%x", req, reason, link_type, prio, sap);
  /* *** next action? */
  return 0;
}

/* Send unidata to DTS */

int sis_uni(struct hi_thr* hit, struct hi_pdu* req)
{
  int confirm, len;
  SIS_LEN_CHECK2(req, unidata_req);
  len = (req->m[SIS_MIN_PDU_SIZE + 10] << 8) & 0x00ff00 | req->m[SIS_MIN_PDU_SIZE + 11] & 0x00ff;
  if (len + SIS_MIN_PDU_SIZE + SIS_UNIHDR_SIZE != req->len) {
    ERR("Bad SIS PDU. fd(%x) u_data_len(%d) inconsistent with s_len(%d)",
	req->fe->fd, len, req->len);
    return HI_CONN_CLOSE;
  }
  
  D("unidata send req(%p)", req);
  if (!prototab[S5066_DTS].specs) {
    ERR("No connection available for DTS %d",0);
    return 0;
  }
  dts_send_uni(hit, prototab[S5066_DTS].specs->conns,
	       req, len, req->m + SIS_MIN_PDU_SIZE + SIS_UNIHDR_SIZE);
  
  confirm = ((struct s_hdr*)req->m)->sprim.unidata_req.delivery_mode.dlvry_cnfrm;
  if (!confirm)
    confirm = (saptab[req->fe->ad.sap].flags >> 2) & 0x3;
  
  switch (confirm) {
  case NO_CONFRM:     /* 0x0 */
  noconf:
    /* The request will eventually be freed when DTS D_PDU is written out. */
    break;
  case NODE_CONFRM:   /* 0x1 */
  case CLIENT_CONFRM: /* 0x2 */
    if (sislocalconfirmhack) {
      sis_send_uni_ok(hit, req->fe, req);
    }
    /* The request will eventually be freed when DTS layer delivers
     * confirmation and we send the SIS confirmation. Need to take care
     * that mere D_PDU is written out does not free request. */
    break;
  case UNDEF_CONFRM:  /* 0x3 */
    D("UNDEF_CONFRM, treating as NO_CONFRM %x", req->fe->fd);
    goto noconf;
  }
  return 0;
}

/* Receive unidata from SIS */

int sis_uni_ind(struct hi_thr* hit, struct hi_pdu* req)
{
  struct hi_io* io;
  int confirm, len, n_in_err, n_no_send, dest_sap;
  char* d;
  SIS_LEN_CHECK2(req, unidata_ind);   /* *** need to handle different sized arq as well */
  dest_sap = ((struct s_hdr*)req->m)->sprim.unidata_ind.dest_sap_id;
  len = (req->m[16] << 8) & 0xff00 | req->m[17] & 0x00ff;
  /*len = ((struct s_hdr*)req->m)->sprim.unidata_ind.size_of_u_pdu; *** struct access has some byte order problem */
  d = req->m + SPRIM_TLEN(unidata_ind);
  n_in_err = (d[0] << 8) & 0xff00 | d[1] & 0x00ff;
  d += 2 + 4 * n_in_err;
  n_no_send = (d[0] << 8) & 0xff00 | d[1] & 0x00ff;
  d += 2 + 4 * n_no_send;
  
  if (d - req->m + len != req->len) {
    ERR("Bad SIS PDU. fd(%x) u_pdu_len(%x) disagrees with s_len(%x)", req->fe->fd, len, req->len);
    HEXDUMP("sis: ", req->m, req->m + req->len, 800);
    return HI_CONN_CLOSE;
  }

  D("unidata_ind sap(%d) req(%p)", dest_sap, req);
  switch (dest_sap) {
  case SAP_ID_HMTP:    smtp_send(hit, req->fe, req, len, d); break;
  default: D("unsupported sap id(%d)", dest_sap);
  }
  /* *** need to send a confirmation to sender? */
  return 0;
}

/* Main dispatch for traffic received from SIS layer. The data can be headed two ways
 * 1. In a router it should be forwarder to DTS layer
 * 2. In a SIS client it should cause the appropriate local action. */

int sis_primitive(struct hi_thr* hit, struct hi_pdu* req)
{
  switch ((req->op = req->m[5])) {
  case S_BIND_REQUEST:              /* 0x01 */  return sis_bind(hit, req);
  case S_UNBIND_REQUEST:            /* 0x02 */  return sis_unbind(hit, req);
  case S_BIND_ACCEPTED:             /* 0x03 */  return sis_bind_ok(hit, req);
  case S_BIND_REJECTED:             /* 0x04 */  return sis_bind_rej(hit, req);
  case S_UNBIND_INDICATION:         /* 0x05 */  return sis_unbind_ind(hit, req);
  case S_HARD_LINK_ESTABLISH:       /* 0x06 */  return sis_hle(hit, req);
  case S_HARD_LINK_TERMINATE:       /* 0x07 */  return sis_hlt(hit, req);
  case S_HARD_LINK_ESTABLISHED:     /* 0x08 */  return sis_hl_ok(hit, req);
  case S_HARD_LINK_REJECTED:        /* 0x09 */  return sis_hl_rej(hit, req);
  case S_HARD_LINK_TERMINATED:      /* 0x0a */  return sis_hlt_ok(hit, req);
  case S_HARD_LINK_INDICATION:      /* 0x0b */  return sis_hl_ind(hit, req);
  case S_HARD_LINK_ACCEPT:          /* 0x0c */  return sis_hla(hit, req);
  case S_HARD_LINK_REJECT:          /* 0x0d */  return sis_hlr(hit, req);
  case S_SUBNET_AVAILABILITY:       /* 0x0e */ break;
  case S_DATA_FLOW_ON:              /* 0x0f */
  case S_DATA_FLOW_OFF:             /* 0x10 */
  case S_KEEP_ALIVE:                /* 0x11 */ break;
  case S_MANAGEMENT_MESSAGE_REQUEST: /* 0x12 */
  case S_MANAGEMENT_MESSAGE_INDICATION: /* 0x13 */ break;
  case S_UNIDATA_REQUEST:           /* 0x14 */  return sis_uni(hit, req);
  case S_UNIDATA_INDICATION:        /* 0x15 */  return sis_uni_ind(hit, req);
  case S_UNIDATA_REQUEST_CONFIRM:   /* 0x16 */
  case S_UNIDATA_REQUEST_REJECTED:  /* 0x17 */
  case S_EXPEDITED_UNIDATA_REQUEST: /* 0x18 */
  case S_EXPEDITED_UNIDATA_INDICATION: /* 0x19 */
  case S_EXPEDITED_UNIDATA_REQUEST_CONFIRM: /* 0x1a */
  case S_EXPEDITED_UNIDATA_REQUEST_REJECTED: /* 0x1b */
    break;
  default:
    ERR("Bad SIS PDU. fd(%x) op(%x) not understood", req->fe->fd, req->op);
    return HI_CONN_CLOSE;
  }
  ERR("Unimplemented SIS PDU. fd(%x) op(%x)", req->fe->fd, req->op);
  hi_free_req_fe(hit, req);
  return 0;
}

int sis_decode(struct hi_thr* hit, struct hi_io* io)
{
  int ret;
  struct hi_pdu* req = io->cur_pdu;
  int n = req->ap - req->m;
  
  if (n < SIS_MIN_PDU_SIZE) {   /* too little, need more */
    req->need = SIS_MIN_PDU_SIZE - n;
    return 0;
  }
  
  if (req->m[0] != (char)0x90 || req->m[1] != (char)0xeb || req->m[2]) { /* 16 bit Maury-Styles Sequence */
    ERR("Bad SIS PDU. fd(%x) need 0x90eb 00 preamble: %x%x %x %x%x", io->fd, req->m[0], req->m[1], req->m[2], req->m[3], req->m[4]);
    return HI_CONN_CLOSE;
  }
  
  req->len = (req->m[3] << 8) | (req->m[4] & 0x00ff); /* exclusive of preamble, version, and len */

  if (req->len > SIS_MAX_PDU_SIZE - SIS_MIN_PDU_SIZE || req->len + SIS_MIN_PDU_SIZE > HI_PDU_MEM) {
    ERR("Bad SIS PDU. fd(%x) length(%d) exceeds SIS_MAX_PDU_SIZE(%d) or HI_PDU_MEM(%d) op(%x)",
	io->fd, req->len, SIS_MAX_PDU_SIZE, HI_PDU_MEM, req->m[5]);
    return HI_CONN_CLOSE;
  }
  
  req->len += SIS_MIN_PDU_SIZE;  /* len is exclusive of preamble and len itself */
  if (n < req->len) {   
    req->need = req->len - n;
    return 0;
  }
  hi_checkmore(hit, io, req, SIS_MIN_PDU_SIZE);
  if (req->len == SIS_MIN_PDU_SIZE) {
    D("Zero length SIS PDU. Ignoring. %x", io->fd);
    hi_free_req(hit, req);
    return 0;
  }
  
  hi_add_to_reqs(io, req);
  if (ret = sis_primitive(hit, req))
    return ret;
  
  return 0;
}

/* EOF  --  sis.c */
