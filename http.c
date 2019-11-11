/* http.c  -  Hype Text Transfer Protocol, 1.0
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * See file COPYING.
 *
 * 15.4.2006, started work over Easter holiday --Sampo
 */

#include "afr.h"
#include "hiios.h"
#include "errmac.h"
#include "s5066.h"

#include <ctype.h>
#include <memory.h>
#include <netinet/in.h> /* htons(3) and friends */

struct hi_pdu* http_encode_start(struct hi_thr* hit)
{
  struct hi_pdu* resp = hi_pdu_alloc(hit);
  if (!resp) { NEVERNEVER("*** out of pdus in bad place %d", 0); }
  return resp;
}

void http_send_err(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int r, char* m)
{
  struct hi_pdu* resp = http_encode_start(hit);
  resp->len = sprintf(resp->m, "HTTP/1.0 %03d %s\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", r, m, strlen(m), m);
  hi_send(hit, io, req, resp);
}

void http_send_data(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int len, char* d)
{
  struct hi_pdu* resp = http_encode_start(hit);
  /*hi_sendv(hit, io, req, resp, len, resp->m, size, req->m + len);*/
}

void http_send_file(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int len, char* d)
{
  struct hi_pdu* resp = http_encode_start(hit);
  /*hi_sendv(hit, io, req, resp, len, resp->m, size, req->m + len);*/
}

#define HTTP_MIN_PDU_SIZE (sizeof("GET / HTTP/1.0\n\n")-1)

int http_decode(struct hi_thr* hit, struct hi_io* io)
{
  struct hi_pdu* req = io->cur_pdu;
  char* url;
  char* url_lim = 0;
  char* p = req->m;
  int n = req->ap - p;
  
  if (n < HTTP_MIN_PDU_SIZE) {   /* too little, need more */
    req->need = HTTP_MIN_PDU_SIZE - n;
    return 0;
  }
  
  if (memcmp(p, "GET /", sizeof("GET /")-1)) {
    ERR("Not a GET HTTP PDU. fd(%x). Got(%.*s)", io->fd, HTTP_MIN_PDU_SIZE, req->m);
    return HI_CONN_CLOSE;
  }

  for (p += 5; p < req->ap - (sizeof(" HTTP/1.0")-2); ++p)
    if (!memcmp(p, " HTTP/1.0\n", sizeof(" HTTP/1.0")-1)) {
      /* Found end of URL */
      url = req->m + 4;
      url_lim = p;
      break;
    }
  
  if (!url_lim) {
    req->need = 1;
    return 0;
  }
  
  io->cur_pdu = 0;
  hi_add_to_reqs(io, req);

  switch (req->m[6]) {
  case 'a': http_send_data(hit, io, req, url_lim-url, url); break;
  case 'b': http_send_file(hit, io, req, url_lim-url, url); break;  /* *** */
  default:  http_send_err(hit, io, req, 500, "Error"); break;
  }
  return 0;
}

/* EOF  --  http.c */
