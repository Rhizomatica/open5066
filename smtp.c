/* smtp.c  -  Simple Mail Transfer Protocol, RFC822
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * This is confidential unpublished proprietary source code of the author.
 * NO WARRANTY, not even implied warranties. Contains trade secrets.
 * Distribution prohibited unless authorized in writing. See file COPYING.
 * $Id: smtp.c,v 1.10 2006/05/26 11:47:32 nito Exp $
 *
 * 15.4.2006, started work over Easter holiday --Sampo
 * 25.4.2006, Viva a revolução! Developed SMTP reception to HMTP proxy side --Sampo
 * 1.5.2006,  Vappu. Progress over first of May weekend --Sampo
 */

#include "afr.h"
#include "hiios.h"
#include "errmac.h"
#include "s5066.h"
#include "sis5066.h"

#include <ctype.h>
#include <memory.h>
#include <netinet/in.h> /* htons(3) and friends */
#include <stdlib.h>

/* ================== SENDING SMTP PRIMITIVES ================== */

extern char remote_station_addr[];

static void hmtp_send(struct hi_thr* hit, struct hi_io* io, int len, char* d, int len2, char* d2)
{
  struct hi_pdu* resp = sis_encode_start(hit, S_UNIDATA_REQUEST,
					 SPRIM_TLEN(unidata_req) + len + len2);
  resp->m[6]  = SAP_ID_HMTP;
  memcpy(resp->m + 7, /*io->ad.dts->remote_station_addr*/ remote_station_addr, 4);
  resp->m[11] = 0x20;    /* nonarq delivery mode */
  resp->m[12] = 0x00;    /* no re tx + infinite TTL */
  resp->m[13] = 0;
  resp->m[14] = 0;
  resp->m[15] = ((len + len2) >> 8) & 0x00ff;
  resp->m[16] =  (len + len2) & 0x00ff;
  D("len=%d len2=%d", len, len2);
  if (len2)
    hi_send3(hit, io, 0, resp, 17, resp->m, len, d, len2, d2);
  else
    hi_send2(hit, io, 0, resp, 17, resp->m, len, d);
}

/* Called from SIS rx layer with u_pdu payload. This could be either HMTP client
 * commands that need to be sent to an SMTP server, or this could be a reply
 * from the remote HMTP server. To make life more difficult, the u_pdu may
 * have been arbitrarily segmented. On response path we need to filter out
 * the HMTP responses that were already given to SMTP server in order to play SMTP.
 * On request path, we need to collect and batch the responses so they can be
 * sent in one go to HMTP pipe. */

void smtp_send(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int len, char* d)
{
  struct hi_pdu* smtp_resp;
  /* Determine role from whether we are listening SMTP or
   * we have SMTP as remote (backend) connection. */
  
  if (!io->pair) {
    struct hi_host_spec* hs;
    struct hi_io* smtp_c;
    /* If we are SMTP server, the pairing will already exist. Thus lack of pairing means
     * we are SMTP client and must open a new connection to remote. */
    hs = prototab[S5066_SMTP].specs;
    if (!hs) {
      ERR("You MUST configure a SMTP remote for HMTP-to-SMTP gateway to work. %d", io->fd);
      exit(1);
    }
    smtp_c = hi_open_tcp(hit->shf, hs, S5066_SMTP);
    if (!smtp_c) {
      ERR("Failed to establish SMTP client connection %x", io->fd);
      return;
    }
    smtp_c->n = hs->conns;
    hs->conns = smtp_c;
    io->pair = smtp_c;
    smtp_c->pair = io;
  }
  
  HEXDUMP("smtp_send: ", d, d+len, 800);
  
  switch (io->pair->qel.kind) { /* Pairing already established, the pair determiones the role. */
  case HI_TCP_S:   /* We are acting as an SMTP server, SIS primitive contains HMTP status  */
    D("HI_TCP_S req(%p) len=%x", req, len);
    /* *** may need to strip away some redundant cruft */
    smtp_resp = hi_pdu_alloc(hit);
    hi_send1(hit, io->pair, 0, smtp_resp, len, d);
    io->pair->ad.smtp.state = SMTP_END;
    break;
  case HI_TCP_C:   /* We are acting as an SMTP client, SIS primitive contains HMTP commands */
    D("HI_TCP_C req(%p) len=%x", req, len);
    req->scan = d;
    io->pair->ad.smtp.uni_ind_hmtp = req;
    io->pair->ad.smtp.state = SMTP_INIT;  /* Wait for 220 greet. */
    return;
  default: NEVERNEVER("impossible pair kind(%d)", io->pair->qel.kind);
  }
  
  /* *** Assemble complete SMTP PDU? This may take several U_PDUs to accomplish. */
}

/* ================== DECODING SMTP PRIMITIVES ================== */

#define CRLF_CHECK(p,lim,req) \
  if (p >= lim) { req->need = 1 + lim - req->m;  return 0; } \
  if (*p == '\r') { \
    ++p; \
    if (p == lim) { req->need = 1 + lim - req->m;  return 0; } \
    if (*p != '\n') goto bad; \
    ++p; \
  } else if (*p == '\n') \
    ++p; \
  else goto bad


static int smtp_ehlo(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req)
{
  char* p = req->m;
  char* lim = req->ap;
  
  if (lim - p < 7) {   /* too little, need more for "EHLO s\n" */
    req->need = 7 - (lim - p);
    return 0;
  }
  
  p[0] = toupper(p[0]);
  p[1] = toupper(p[1]);
  p[2] = toupper(p[2]);
  p[3] = toupper(p[3]);
  if (memcmp(p, "EHLO ", 5) && memcmp(req->m, "HELO ", 5)) goto bad;
  p += 5;
  
  for (; p < lim && !ONE_OF_2(*p, '\r', '\n'); ++p) ;
  CRLF_CHECK(p, lim, req);

  hi_sendf(hit, io, "250-%s\r\n250-PIPELINING\r\n250 8-BIT MIME\r\n", SMTP_EHLO_CLI);
  io->pair = prototab[S5066_SIS].specs->conns;
  prototab[S5066_SIS].specs->conns->pair = io;  /* But there could be multiple? */
#if 0   /* We do this nowdays during setup */
  sis_send_bind(hit, io->pair, SAP_ID_HMTP, 0, 0x0200);  /* 0x0200 == nonarq, no repeats */
#endif
  io->ad.smtp.state = SMTP_MAIN;
  req->need = (p - req->m) + 5;
  req->ad.smtp.skip_ehlo = req->scan = p;
  D("EHLO ok req(%p)", req);
  return 0;
 bad:
  ERR("Bad SMTP PDU. fd(%x)", io->fd);
  return HI_CONN_CLOSE;
}

static int smtp_mail_from(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req)
{
  char* p = req->scan;
  char* lim = req->ap;

  if (lim - p < 5) {
    req->need = 5 - (lim - p);
    return 0;
  }

  p[0] = toupper(p[0]);
  p[1] = toupper(p[1]);
  p[2] = toupper(p[2]);
  p[3] = toupper(p[3]);
  
  if (!memcmp(p, "QUIT", 4)) {
    p += 4;
    CRLF_CHECK(p, lim, req);
    hi_sendf(hit, io, "221 bye\r\n");
    return HI_CONN_CLOSE;
  }
  
  if (lim - p < 11) {
    req->need = 11 - (lim - p);
    return 0;
  }
  
  p[5] = toupper(p[5]);
  p[6] = toupper(p[6]);
  p[7] = toupper(p[7]);
  p[8] = toupper(p[8]);
  
  if (memcmp(p, "MAIL FROM:", 10)) goto bad;  
  p += 10;
  
  for (; p < lim && !ONE_OF_3(*p, '>', '\r', '\n'); ++p) ;
  if (p == lim) { req->need = 1;  return 0; }
  if (*p != '>') goto bad;
  ++p;
  CRLF_CHECK(p, lim, req);
  hi_sendf(hit, io, "250 sok\r\n");
  io->ad.smtp.state = SMTP_TO;
  req->need = (p - req->m) + 5;   /* "DATA\n" */
  req->scan = p;
  D("MAIL FROM ok req(%p)", req);
  return 0;
 bad:
  ERR("Bad SMTP PDU(%p). fd(%x)", req, io->fd);
  HEXDUMP("p: ", p, lim, 50);
  HEXDUMP("m: ", req->m, lim, 50);
  return HI_CONN_CLOSE;
}

static int smtp_rcpt_to(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req)
{
  char* p = req->scan;
  char* lim = req->ap;

  if (lim - p < 5) {   /* "DATA\n" */
    req->need = 5 - (lim - p);
    return 0;
  }
  
  p[0] = toupper(p[0]);
  p[1] = toupper(p[1]);
  p[2] = toupper(p[2]);
  p[3] = toupper(p[3]);
  
  if (!memcmp(p, "QUIT", 4)) {
    p += 4;
    CRLF_CHECK(p, lim, req);
    hi_sendf(hit, io, "221 bye\r\n");
    return HI_CONN_CLOSE;
  }

  if (!memcmp(p, "DATA", 4)) {
    p += 4;
    CRLF_CHECK(p, lim, req);
    hi_sendf(hit, io, "354 end with .\r\n");
    io->ad.smtp.state = SMTP_MORE1;
    req->need = (p - req->m) + 2; /* .\n */
    req->scan = p-1;  /* leave \n to be scanned to avoid beginning of mail special case */
    D("DATA seen req(%p) need=%d", req, req->need);
    return 0;
  }
  
  if (lim - p < 12) {   /* "RCPT TO:<x>\n" */
    req->need = 12 - (lim - p);
    return 0;
  }
  
  p[5] = toupper(p[5]);
  p[6] = toupper(p[6]);
  
  if (memcmp(p, "RCPT TO:", 8)) goto bad;  
  p += 8;
  
  for (; p < lim && !ONE_OF_3(*p, '>', '\r', '\n'); ++p) ;
  if (p == lim) { req->need = 1;  return 0; }
  if (*p != '>') goto bad;
  ++p;
  CRLF_CHECK(p, lim, req);
  hi_sendf(hit, io, "250 rok\r\n");
  req->need = (p - req->m) + 5;
  req->scan = p;
  D("RCPT TO ok req(%p)", req);
  return 0;
  
 bad:
  ERR("Bad SMTP PDU. fd(%x)", io->fd);
  return HI_CONN_CLOSE;
}

static int smtp_data(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req)
{
  char* p = req->scan;
  char* lim = req->ap;
  
  switch (io->ad.smtp.state) {
  case SMTP_MORE0: break;
  case SMTP_MORE1: goto look_for_dot;
  case SMTP_MORE2:
  default: NEVERNEVER("impossible SMTP state %d", io->ad.smtp.state);
  }
  
  while (lim - p >= 3) {    /* \n.\n */
    for (; *p != '\n' && lim - p >= 3; ++p) ; /* \n.\n */
    if (lim - p < 3)
      break;
    ++p;
  look_for_dot:
    if (p[0] == '.' && ONE_OF_2(p[1], '\r', '\n')) {
      ++p;
      if (*p == '\r') {
	++p;
	if (p == lim) break;
	if (*p != '\n') continue;   /* this happens a lot */
      }
      ++p;   /* *p was '\n' */
      
      /* End of message, hurrah! */
      
      D("End-of-message seen req(%p)", req);
      hmtp_send(hit, io->pair, p - req->m, req->m, 6, "QUIT\r\n");
#if 1
      io->ad.smtp.state = SMTP_WAIT;
      req->need = 0;  /* Hold it until we get response from SIS layer. */
#else
      hi_sendf(hit, io, "250 sent\r\n");   /* *** hold this off? */
      req->need = (p - req->m) + 5;
      /* *** not clear how second message could be sent. Perhaps we need second scan pointer? */
#endif
      req->scan = p;
      return 0;
    }
  }
  /* *** need to handle mail larger than U_PDU case */
  req->need = 3 - (lim - p);
  req->scan = p-1;
  D("more data needed req(%p) need=%d", req, req->need);
  return 0;
  
 bad:
  ERR("Bad SMTP PDU. fd(%x)", io->fd);
  return HI_CONN_CLOSE;
}

int smtp_decode_req(struct hi_thr* hit, struct hi_io* io)
{
  int ret;
  char* p;
  struct hi_pdu* req = io->cur_pdu;
  D("smtp_state(%d) scan(%.*s)", io->ad.smtp.state, MIN(7, req->ap - req->scan), req->scan);
  switch (io->ad.smtp.state) {
  case SMTP_START:  return smtp_ehlo(hit, io, req);
  case SMTP_MAIN:   return smtp_mail_from(hit, io, req);
  case SMTP_TO:     return smtp_rcpt_to(hit, io, req);
  case SMTP_MORE0:
  case SMTP_MORE1:
  case SMTP_MORE2:  return smtp_data(hit, io, req);
  case SMTP_WAIT:
  case SMTP_STATUS: D("Unexpected state %x", io->ad.smtp.state);
  case SMTP_END:    return smtp_mail_from(hit, io, req);
  default: NEVERNEVER("impossible SMTP state %d", io->ad.smtp.state);
  }
  return 0;
}

/* ========= Process responses from SMTP server ========= */

/* The responses are generally in response to parallel process where HMTP is received from
 * SIS layer and written as SMTP commands to the server. Onve we have enough responses,
 * we need to send HMTP response using SIS layer. Generally this is detected
 * by recognizing "354 enter mail" response followed by "250 sent". Earlier 250 responses
 * must not trigger HMTP pdu. Any other response than 250, 354, or 221 quit triggers
 * HMTP error response. */

static int smtp_resp_wait_220_greet(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* resp)
{
  char* p = resp->scan;
  char* lim = resp->ap;
  int n = lim - p;
  
  if (n < 6) {  /* 220 m\n or 220-m\n */
    resp->need = (6 - n) + (p - resp->m);  /* what we have plus what we need */
    return 0;
  }
  
  if (!THREE_IN_ROW(p, '2', '2', '0'))  /* 220 greet */
    goto bad;
  
  switch (n = p[3]) {
  case ' ':
  case '-':
    for (p+=4; p < lim && !ONE_OF_2(*p, '\r', '\n'); ++p) ;
    CRLF_CHECK(p, lim, resp);
    resp->scan = p;
    break;
  default: goto bad;
  }
  if (n == ' ') {
    D("220 greet seen resp(%p)", resp);
    hi_sendf(hit, io, "EHLO %s\r\n", SMTP_GREET_DOMAIN);
    io->ad.smtp.state = SMTP_RDY;
  }
  resp->need = 6 + p - resp->m;  /* Prime the pump for next response */
  return 0;
  
 bad:
  D("SMTP server sent bad response(%.*s)", n, p);
  if (io->pair)
    hmtp_send(hit, io->pair, resp->len, resp->m, 0, 0);
  return HI_CONN_CLOSE;
}

static int smtp_resp_wait_250_from_ehlo(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* resp)
{
  char* p = resp->scan;
  char* lim = resp->ap;
  int n = lim - p;
  
  if (n < 6) {  /* 250 m\n or 250-m\n */
    resp->need = (6 - n) + (p - resp->m);  /* what we have plus what we need */
    return 0;
  }
  
  if (!THREE_IN_ROW(p, '2', '5', '0'))
    goto bad;
  
  switch (n = p[3]) {
  case ' ':
  case '-':
    for (p+=4; p < lim && !ONE_OF_2(*p, '\r', '\n'); ++p) ;
    CRLF_CHECK(p, lim, resp);
    resp->scan = p;
    break;
  default: goto bad;
  }
  if (n == ' ') {
    if (io->ad.smtp.uni_ind_hmtp) {
      /* Send payload immediately */
      /* io->ad.smtp.uni_ind_hmtp is the sis unidata_ind primitive contaning HMTP request */
      char* payload;
      char* q = io->ad.smtp.uni_ind_hmtp->scan;
      char* qlim = io->ad.smtp.uni_ind_hmtp->ap;
      struct hi_pdu* pdu = hi_pdu_alloc(hit);
      
      if (qlim-q < 25)   /* *** should determine this number better */
	goto badhmtp;
      
      /* Skip EHLO if any */
      
      q[0] = toupper(q[0]);
      q[1] = toupper(q[1]);
      q[2] = toupper(q[2]);
      q[3] = toupper(q[3]);
      
      if (memcmp(q, "EHLO ", 5))
	goto badhmtp;

      for (q+=5; q < qlim && !ONE_OF_2(*q, '\r', '\n'); ++q) ;
      if (q == qlim)
	goto badhmtp;
      if (*q == '\r') {
	++q;
	if (q == qlim || *q != '\n')
	  goto badhmtp;
      }
      ++q;
      if (q == qlim)
	goto badhmtp;	
      
      payload = q;
      
      /* Scan till end of DATA command. We can not send the actual data before 354 response
       * to DATA command, for which we wait in SEND state. */

      for (; q+6 < qlim; ++q) {
	if (q[0] == '\n'
	    && ONE_OF_2(q[1], 'D', 'd')
	    && ONE_OF_2(q[2], 'A', 'a')
	    && ONE_OF_2(q[3], 'T', 't')
	    && ONE_OF_2(q[4], 'A', 'a')
	    && ONE_OF_2(q[5], '\r', '\n')
	    ) {
	  if (q[5] == '\r' && ((q+7 >= qlim) || q[6] != '\n'))
	    continue;
	  q += (q[5] == '\r') ? 7 : 6;
	  break;
	}
      }
      
      D("250 for EHLO seen resp(%p)", resp);
      io->ad.smtp.uni_ind_hmtp->scan = q;
      hi_send1(hit, io, 0, pdu, q-payload, payload);
      io->ad.smtp.state = SMTP_SEND;
      /* *** if hmtp / smtp message was not complete, arrange further SIS layer
       *     I/O to be forwarded into the smtp connection. Similarily, if the
       *     hmtp message has not arrived yet at all, it should be forwarded
       *     as soon as it does arrive. */
    } else {
      NEVER("smtp client io is missing is unidata_ind_hmtp? %p", io->pair);
      return HI_CONN_CLOSE;
    }
  }
  resp->need = 6 + p - resp->m;  /* Prime the pump for next response */
  return 0;
  
 bad:
  D("SMTP server sent bad response(%.*s)", n, p);
  if (io->pair)
    hmtp_send(hit, io->pair, resp->len, resp->m, 0, 0);
  return HI_CONN_CLOSE;
 badhmtp:
  D("Bad HMTP PDU from SIS layer %d", 0);
  if (io->pair)
    hmtp_send(hit, io->pair, 9, "500 Bad\r\n", 0, 0);
  return HI_CONN_CLOSE;
}

static int smtp_resp_wait_354_from_data(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* resp)
{
  char* p = resp->scan;
  char* lim = resp->ap;
  int next_state, n = lim - p;
  
  if (n < 6) {  /* 250 m\n or 250-m\n */
    resp->need = (6 - n) + (p - resp->m);  /* what we have plus what we need */
    return 0;
  }
  
  if        (THREE_IN_ROW(p, '3', '5', '4')) {  /* 354 enter mail */
    next_state = SMTP_SENT;
  } else if (THREE_IN_ROW(p, '2', '5', '0')) {
    next_state = io->ad.smtp.state;
  } else
    goto bad;
  
  switch (n = p[3]) {
  case ' ':
  case '-':
    for (p+=4; p < lim && !ONE_OF_2(*p, '\r', '\n'); ++p) ;
    CRLF_CHECK(p, lim, resp);
    resp->scan = p;
    break;
  default: goto bad;
  }
  if (n == ' ' && next_state == SMTP_SENT) {
    if (io->ad.smtp.uni_ind_hmtp) {
      /* Send payload immediately */
      /* io->ad.smtp.uni_ind_hmtp is the sis unidata_ind primitive contaning HMTP request */
      char* payload;
      char* q = io->ad.smtp.uni_ind_hmtp->scan;
      char* qlim = io->ad.smtp.uni_ind_hmtp->ap;
      struct hi_pdu* pdu = hi_pdu_alloc(hit);
      
      payload = q;
      --q;  /* Take the new line from preceding DATA to avoid special case later */
      
      /* Make sure QUIT is NOT sent (we will send one ourselves, eventually). Scan for message
       * terminating "\r\n.\r\n". Its also possible we will not see message terminator. That
       * means the message is so big it takes several SIS primitives to transmit. */
      
      for (; q+2 < qlim; ++q) {
	if (q[0] == '\n' && q[1] == '.' && ONE_OF_2(q[2], '\r', '\n')) {
	  if (q[2] == '\r' && ((q+3 >= qlim) || q[3] != '\n'))
	    continue;
	  q += (q[2] == '\r') ? 4 : 3;
	  break;
	}
      }
      
      D("354 for DATA seen resp(%p), sending %d bytes", resp, q-payload);
      hi_send1(hit, io, 0, pdu, q-payload, payload);
      io->ad.smtp.state = SMTP_SENT;
      /* *** if hmtp / smtp message was not complete, arrange further SIS layer
       *     I/O to be forwarded into the smtp connection. Similarily, if the
       *     hmtp message has not arrived yet at all, it should be forwarded
       *     as soon as it does arrive. */
    } else {
      NEVER("smtp client io is missing is unidata_ind_hmtp? %p", io->pair);
      return HI_CONN_CLOSE;
    }
  }
  resp->need = 6 + p - resp->m;  /* Prime the pump for next response */
  return 0;
  
 bad:
  D("SMTP server sent bad response(%.*s)", n, p);
  if (io->pair)
    hmtp_send(hit, io->pair, resp->len, resp->m, 0, 0);
  return HI_CONN_CLOSE;
 badhmtp:
  D("Bad HMTP PDU from SIS layer %d", 0);
  if (io->pair)
    hmtp_send(hit, io->pair, 9, "500 Bad\r\n", 0, 0);
  return HI_CONN_CLOSE;
}

static int smtp_resp_wait_250_msg_sent(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* resp)
{
  char* p = resp->scan;
  char* lim = resp->ap;
  int n = lim - p;
  
  if (n < 6) {  /* 250 m\n or 250-m\n */
    resp->need = (6 - n) + (p - resp->m);  /* what we have plus what we need */
    return 0;
  }
  
  if (!THREE_IN_ROW(p, '2', '5', '0'))     /* 250 message sent */
    goto bad;
  
  switch (n = p[3]) {
  case ' ':
  case '-':
    for (p+=4; p < lim && !ONE_OF_2(*p, '\r', '\n'); ++p) ;
    CRLF_CHECK(p, lim, resp);
    resp->scan = p;
    break;
  default: goto bad;
  }
  if (n == ' ') {
    /* *** should we attempt to skip the 220 greeting? */
    D("250 after data 354 seen resp(%p)", resp);
    hmtp_send(hit, io->pair, p-resp->m, resp->m, 13, "221 goodbye\r\n");
    hi_sendf(hit, io, "QUIT\r\n");   /* One message per connection! */
    io->ad.smtp.state = SMTP_QUIT;
  }
  resp->need = 6 + p - resp->m;  /* Prime the pump for next response */
  return 0;
  
 bad:
  D("SMTP server sent bad response(%.*s)", n, p);
  if (io->pair)
    hmtp_send(hit, io->pair, resp->len, resp->m, 0, 0);
  return HI_CONN_CLOSE;
}

static int smtp_resp_wait_221_goodbye(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* resp)
{
  char* p = resp->scan;
  char* lim = resp->ap;
  int n = lim - p;
  
  if (n < 6) {  /* 250 m\n or 250-m\n */
    resp->need = (6 - n) + (p - resp->m);  /* what we have plus what we need */
    return 0;
  }
  
  if (!THREE_IN_ROW(p, '2', '2', '1'))     /* 221 goodbye */
    goto bad;
  
  switch (n = p[3]) {
  case ' ':
  case '-':
    for (p+=4; p < lim && !ONE_OF_2(*p, '\r', '\n'); ++p) ;
    CRLF_CHECK(p, lim, resp);
    resp->scan = p;
    break;
  default: goto bad;
  }
  if (n == ' ') {
    D("221 bye seen resp(%p)", resp);
    io->ad.smtp.state = SMTP_INIT;
    return HI_CONN_CLOSE;
  }
  resp->need = 6 + p - resp->m;  /* Prime the pump for next response */
  return 0;
  
 bad:
  D("SMTP server sent bad response(%.*s)", n, p);
  return HI_CONN_CLOSE;
}

int smtp_decode_resp(struct hi_thr* hit, struct hi_io* io)
{
  int ret;
  char* p;
  struct hi_pdu* resp = io->cur_pdu;
  D("smtp_state(%d) scan(%.*s)", io->ad.smtp.state, MIN(7, resp->ap - resp->scan), resp->scan);
  switch (io->ad.smtp.state) {
  case SMTP_INIT: return smtp_resp_wait_220_greet(hit, io, resp);
  case SMTP_EHLO: D("Unexpected state %x", io->ad.smtp.state);
  case SMTP_RDY:  return smtp_resp_wait_250_from_ehlo(hit, io, resp);
  case SMTP_MAIL:
  case SMTP_RCPT:
  case SMTP_DATA: D("Unexpected state %x", io->ad.smtp.state);
  case SMTP_SEND: return smtp_resp_wait_354_from_data(hit, io, resp);
  case SMTP_SENT: return smtp_resp_wait_250_msg_sent(hit, io, resp);
  case SMTP_QUIT: return smtp_resp_wait_221_goodbye(hit, io, resp);
  default: NEVERNEVER("impossible SMTP state %d", io->ad.smtp.state);
  }
  return 0;
}

/* EOF  --  smtp.c */

