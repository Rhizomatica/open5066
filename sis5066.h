#ifndef _SIS5066_H
#define _SIS5066_H
/*
 * Definitons for the Subnetwork Interface Sublayer of the
 * STANAG 5066 Version 1.2 standard.  STANAG 5066 is a profile 
 * for High Frequency (HF) Radio Data Communications.
 *
 * Copyrights (c) 1999/2000 NATO C3 Agency, CSD-R,
 *                          Radio Protocols Lab
 *                          http://www.nc3a.nato.int/
 *
 * Written by Jan-Willem Smaal <Jan-Willem.Smaal@nc3a.nato.int>
 * (student TH-Rijswijk) during a traineeship at the NC3A.
 *
 * See file COPYING_sis5066_h.
 */

/*
 * -------------------------------------------------------
 *          PLEASE MODIFY THE LINES BELOW
 * Please set at most one of these defines to '1'
 * -------------------------------------------------------
 */

#ifdef __WIN32__
#  define GECMARCONI 0		    	/* S5066 version 1.0 TCP port 9999*/
#  define MARCONIMOBILE 0		    /* S5066 version 1.2 TCP port 9999*/
#  define ROCKWELL 0			      /* S5066 version 1.2 TCP port 55555*/
#  define ROCKWELL_OFFS_FIX 1		/* S5066 version 1.2 (with 4 extra octets fix) */
#  define NC3A_SIMULATOR	0	    /* S5066 version 1.2 (simulated stack) */
#endif

/* #define __USE_BSD */

#define ROCKWELL_OFFS_FIX 0

/* 
 * ------------------------------------------------------- 
 * You normally don't need to modify anything below this
 * line unless you know what you're doing ofcourse :-).
 * ------------------------------------------------------- 
 */

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef BYTE_ORDER
#include <endian.h>
#endif

/* Set default listening TCP port for the different stacks */
#define S5066_V1_2
#define STD_STANAG_TCP_PORT 5066


/*
 * Somehow the Rockwell Collins HF messenger STANAG 5066 
 * stack adds 4 0x00 octets to received S_UNIDATA_INDICATION 
 * packets on the subnet.  (The problem is being investigated)
 */
#if ROCKWELL_OFFS_FIX
#  define OFFS_FIX +4
#else
#  define OFFS_FIX +0
#endif

/*
 * Packets are one octet shorter for STANAG 5066 v1.0
 */
#if defined(S5066_V1_0)
#  define OFFS -1
#else
#  define OFFS -0
#endif



/* 
 * This is the value for the STANAG-5066 version field
 * it's transmitted after the maury style preamble.
 * Currently (28 August 1999) STANAG-5066 version 1.2
 * the version field is defined as all zero's.
 */
#define VERS_FLD 0x00

/* Maximal Subnet Transfer Unit (SMTU) */
#define SMTU (4 * 1024)



/*****************************************/
/******* STANAG 5066 ANNEX A *************/
/*****************************************/



/*****************************************/
/******* S_ PRIMITIVE TYPES **************/
/*****************************************/
#define S_BIND_REQUEST 		0x01
#define S_UNBIND_REQUEST 	0x02
#define S_BIND_ACCEPTED 	0x03
#define S_BIND_REJECTED 	0x04
#define S_UNBIND_INDICATION 	0x05
#define S_HARD_LINK_ESTABLISH 	0x06
#define S_HARD_LINK_TERMINATE 	0x07
#define S_HARD_LINK_ESTABLISHED 0x08
#define S_HARD_LINK_REJECTED 	0x09
#define S_HARD_LINK_TERMINATED 	0x0a
#define S_HARD_LINK_INDICATION 	0x0b
#define S_HARD_LINK_ACCEPT 	0x0c
#define S_HARD_LINK_REJECT 	0x0d
#define S_SUBNET_AVAILABILITY 	0x0e
#define S_DATA_FLOW_ON 		0x0f
#define S_DATA_FLOW_OFF 	0x10
#define S_KEEP_ALIVE 		0x11
#define S_MANAGEMENT_MESSAGE_REQUEST 	0x12
#define S_MANAGEMENT_MESSAGE_INDICATION 0x13
#define S_UNIDATA_REQUEST 	0x14
#define S_UNIDATA_INDICATION 	0x15
#define S_UNIDATA_REQUEST_CONFIRM 	0x16
#define S_UNIDATA_REQUEST_REJECTED 	0x17
#define S_EXPEDITED_UNIDATA_REQUEST 	0x18
#define S_EXPEDITED_UNIDATA_INDICATION 	0x19
#define S_EXPEDITED_UNIDATA_REQUEST_CONFIRM 	0x1a
#define S_EXPEDITED_UNIDATA_REQUEST_REJECTED 	0x1b



/*****************************************/
/******* ENCODING OF COMMON FIELDS *******/
/*****************************************/

/*
 * Maury style preamble
 */
#if BYTE_ORDER == BIG_ENDIAN
#  define PREAMBLE 0x90eb
#elif BYTE_ORDER == LITTLE_ENDIAN
#  define PREAMBLE 0xeb90
#endif


/*****************************************/
/******* ADDRESS MACROS ******************/
/*****************************************/
#define GROUP_ADDR	0x10	/* 0001 0000 b */
#define MAX_ADDR_SIZE	0xe0	/* 1110 0000 b */
#define ADDR_SIZE(size) 	(((size << 5)&MAX_ADDR_SIZE))


/*
 * service type delivery mode field as used in S_UNIDATA_REQ and
 * S_EXEDITED_UNIDATA_REQ packet
 */
#pragma pack(1)
struct service_type {
/* Transmission mode */
#define IGN_TX_MODE 	0x0
#define ARQ_TX_MODE 	0x1
#define NON_ARQ_TX_MODE 0x2
#define OTHER_TX_MODE   0x3
/* Delivery confirmation */
#define NO_CONFRM	0x0
#define NODE_CONFRM	0x1
#define CLIENT_CONFRM	0x2
#define UNDEF_CONFRM	0x3
/* Delivery order */
#define IN_ORDR		0x0
#define AS_ARRIVE_ORDR	0x1
/* Extended field */
#define NO_XTND_FLD	0x0
#define XTND_FLD_FLWS	0x1
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int tx_mode:4;
	unsigned int dlvry_cnfrm:2;
	unsigned int dlvry_ordr:1;
	unsigned int ext_fld:1;

	unsigned int no_retxs:4;
	unsigned int not_used:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int ext_fld:1;
	unsigned int dlvry_ordr:1;
	unsigned int dlvry_cnfrm:2;
	unsigned int tx_mode:4;

	unsigned int not_used:4;
	unsigned int no_retxs:4;
#endif
};

#pragma pack()



/*****************************************/
/******* ENCODING OF S PRIMITIVES ********/
/*****************************************/

/* S_BIND_REQUEST packet */
#pragma pack(1)
struct bind_request {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int sap_id:4;
	unsigned int rank:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int rank:4;
	unsigned int sap_id:4;
#endif
	struct service_type service_type;
};

#pragma pack()




/* S_UNBIND_REQUEST packet */
#pragma pack(1)
struct unbind_request {
	unsigned char type;
};

#pragma pack()




/* S_BIND_ACCEPTED packet */
#pragma pack(1)
struct bind_accepted {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int sap_id:4;
	unsigned int not_used:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int not_used:4;
	unsigned int sap_id:4;
#endif
	unsigned short mtu;
};

#pragma pack()




/* S_BIND_REJECTED packet */
#pragma pack(1)
struct bind_rejected {
	unsigned char type;
#define INSUFF_RES 		0x01
#define INVALID_SAP_ID 		0x02
#define SAP_ALRDY_ALLOC 	0x03
#define NO_ARQ_DURING_BCAST 	0x04
	unsigned char reason;
};

#pragma pack()




/* S_UBIND_INDICATION packet */
#pragma pack(1)
struct unbind_indication {
	unsigned char type;
#define CON_PREEMPTED_BY_HIGHER_RNK 0x01
#define NO_RESP_TO_KEEP_ALIVE	    0x02
#define TOO_MANY_INVLD_PRIMTVS	    0x03
#define TOO_MANY_EXPIDITED_REQ_PRIM 0x04
#define ARQ_MODE_UNSUPPORTABLE      0x05
	unsigned char reason;
};

#pragma pack()



/*
 * Hard Link Data Exchange definitions
 */

/* Session types */
#define PHYS_LNK_RES	0x0
#define PART_BW_RES	0x1
#define FULL_BW_RES	0x2
#define BCAST_SESSION 	0x3


/* S_HARD_LINK_ESTABLISH packet */
#pragma pack(1)
struct hard_link_establish {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int link_type:2;
	unsigned int link_priority:2;
	unsigned int remote_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int remote_sap_id:4;
	unsigned int link_priority:2;
	unsigned int link_type:2;
#endif
	unsigned char remote_node[4];
};

#pragma pack()




/* S_HARD_LINK_TERMINATE packet */
#pragma pack(1)
struct hard_link_terminate {
	unsigned char type;
	unsigned char remote_node[4];
};

#pragma pack()



/* S_HARD_LINK_ESTABLISHED packet */
#pragma pack(1)
struct hard_link_established {
	unsigned char type;
#define HLINK_ERROR 0
#define HLINK_OK    > 1
	unsigned char remote_node_status;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int link_type:2;
	unsigned int link_priority:2;
	unsigned int remote_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int remote_sap_id:4;
	unsigned int link_priority:2;
	unsigned int link_type:2;
#endif
	unsigned char remote_node[4];
};

#pragma pack()



/* S_HARD_LINK_REJECTED packet */
#pragma pack(1)
struct hard_link_rejected {
	unsigned char type;
#define REM_NODE_BUSY		0x01
#define HIGH_PRIO_LNK_EXISTS 	0x02
#define REM_NODE_NOT_RESPONDING 0x03
#define DEST_SAP_ID_NOT_BND	0x04
#define REQ_LNK_TYPE0_EXISTS	0x05
	unsigned char reason;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int link_type:2;
	unsigned int link_priority:2;
	unsigned int remote_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int remote_sap_id:4;
	unsigned int link_priority:2;
	unsigned int link_type:2;
#endif
	unsigned char remote_node[4];
};

#pragma pack()




/* S_HARD_LINK_TERMINATED packet */
#pragma pack(1)
struct hard_link_terminated {
	unsigned char type;
#define LNK_TERM_BY_REM_NODE 	0x01
#define HIGH_PRIO_LNK_REQ	0x02
#define REMOTE_NODE_NOT_RESP	0x03
#define DEST_SAP_ID_UNBOUND	0x04
#define PHYSICAL_LNK_BROKEN	0x05
	unsigned char reason;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int link_type:2;
	unsigned int link_priority:2;
	unsigned int remote_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int remote_sap_id:4;
	unsigned int link_priority:2;
	unsigned int link_type:2;
#endif
	unsigned char remote_node[4];
};

#pragma pack()



/* S_HARD_LINK_INDICATION packet */
#pragma pack(1)
struct hard_link_indication {
	unsigned char type;
	unsigned int remote_node_status:8;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int link_type:2;
	unsigned int link_priority:2;
	unsigned int remote_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int remote_sap_id:4;
	unsigned int link_priority:2;
	unsigned int link_type:2;
#endif
	unsigned char remote_node[4];
};

#pragma pack()



/* S_HARD_LINK_ACCEPT packet */
#pragma pack(1)
struct hard_link_accept {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int link_type:2;
	unsigned int link_priority:2;
	unsigned int remote_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int remote_sap_id:4;
	unsigned int link_priority:2;
	unsigned int link_type:2;
#endif
	unsigned char remote_node[4];
};

#pragma pack()



/* S_HARD_LINK_REJECT packet */
#pragma pack(1)
struct hard_link_reject {
	unsigned char type;
	unsigned char reason;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int link_type:2;
	unsigned int link_priority:2;
	unsigned int remote_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int remote_sap_id:4;
	unsigned int link_priority:2;
	unsigned int link_type:2;
#endif
	unsigned char remote_node[4];
};

#pragma pack()



/* S_SUBNET_AVAILABILITY packet */
#pragma pack(1)
struct subnet_availability {
	unsigned char type;
	unsigned int node_status:8;
	unsigned char reason;
};

#pragma pack()



/* S_DATA_FLOW_ON packet */
#pragma pack(1)
struct data_flow_on {
	unsigned char type;
};

#pragma pack()



/* S_DATA_FLOW_OFF packet */
#pragma pack(1)
struct data_flow_off {
	unsigned char type;
};

#pragma pack()



/* S_KEEP_ALIVE packet */
#pragma pack(1)
struct keep_alive {
	unsigned char type;
};

#pragma pack()



/* S_MANAGEMENT_MESSAGE_REQUEST packet */
#pragma pack(1)
struct management_message_request {
	unsigned char type;
	unsigned char msg_type;
};

#pragma pack()



/* S_MANAGEMENT_MESSAGE_INDICATION packet */
#pragma pack(1)
struct management_message_indication {
	unsigned char type;
	unsigned int msg_type:8;
};

#pragma pack()

/* STANAG 5066 ANNEX A.2.2.28.2 */
#pragma pack(1)
struct delivery_mode_field
{
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int tx_mode:4;
	unsigned int dlvry_cnfrm:2;
	unsigned int dlvry_ordr:1;
	unsigned int ext_fld:1;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int ext_fld:1;
	unsigned int dlvry_ordr:1;
	unsigned int dlvry_cnfrm:2;
	unsigned int tx_mode:4;
#endif
};
#pragma pack()


/* S_UNIDATA_REQUEST packet */
#pragma pack(1)
struct unidata_req {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int priority:4;
	unsigned int sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int sap_id:4;
	unsigned int priority:4;
#endif
	unsigned char addr_fld[4];
	/* struct service_type service_type; */
  struct delivery_mode_field delivery_mode;
  /* 
	 * Now the "dirty-part(tm)"
	 * The STANAG defines ttl as an 20 bit long "long" only the 
	 * problem is that the boundry is not aligned on a byte thus 
	 * we have to fiddle with machine dependant byte ordering stuff... 
	 * If we don't, the MSB of ttl and no_rtxs get swapped on an Intel 
	 * machine (since Intel is Little Endian) as I had to find out the
	 * hard way :-(.
	 */
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int no_retxs:4;
	unsigned int ttlmsb:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int ttlmsb:4;
	unsigned int no_retxs:4;
#endif
	unsigned short ttl;
	/* */
	unsigned short size_of_pdu;
};

#pragma pack()



/* S_UNIDATA_INDICATION packet (*not* NON_ARQ W/RRORS type!) */
#pragma pack(1)
struct unidata_ind {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int priority:4;
	unsigned int dest_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int dest_sap_id:4;
	unsigned int priority:4;
#endif
	unsigned char dest_node[4];
/* Transmission types used in unidata indication */
#define TX_NOT_USED 	0x0
#define TX_ARQ		0x1
#define TX_NON_ARQ	0x2
#define TX_NON_ARQ_WERR 0x3
#define TX_TO_BE_DEF	0x4
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int tx_mode:4;
	unsigned int source_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int source_sap_id:4;
	unsigned int tx_mode:4;
#endif
	unsigned char source_node[4];
	unsigned short size_of_u_pdu;
};

#pragma pack()


/* S_UNIDATA_INDICATION packet (*with* NON_ARQ W/RRORS type!) */
#pragma pack(1)
struct unidata_ind_non_arq {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int priority:4;
	unsigned int dest_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int dest_sap_id:4;
	unsigned int priority:4;
#endif
	unsigned char dest_node[4];
/* Transmission types used in unidata indication */
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int tx_mode:4;
	unsigned int source_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int source_sap_id:4;
	unsigned int tx_mode:4;
#endif
	unsigned char source_node[4];
	unsigned short size_of_u_pdu;
	unsigned short k_err_blks;
	/* k ordered pairs follow hereafter */
	unsigned short l_non_recvd_blks;
	/* l ordered pairs follow hereafter */
};

#pragma pack()


/* S_UNIDATA_CONFIRM */
#pragma pack(1)
struct unidata_req_confirm {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int not_used:4;
	unsigned int dest_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int dest_sap_id:4;
	unsigned int not_used:4;
#endif
	unsigned char dest_node[4];
	unsigned int size_of_u_pdu:16;
};

#pragma pack()



/* S_UNIDATA_REQUEST_REJECTED */
#pragma pack(1)
struct unidata_req_rejected {
	unsigned char type;
#define TTL_EXPIRED 		0x1
#define DEST_SAP_ID_NOT_BOUND	0x2
#define DEST_NODE_NOT_RESP	0x3
#define U_PDU_LARGER_MTU	0x4
#define TX_MODE_NOT_SPECIFIED 	0x5
/** Symbolic Constant- the data time-to-live had expired (unidata_req_rejected) -- Proposed for S'5066 Edition 2*/
#define TX_WINDOW_BLOCKED 	0x6
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int reason:4;
	unsigned int dest_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int dest_sap_id:4;
	unsigned int reason:4;
#endif
	unsigned char dest_node[4];
	unsigned short size_of_u_pdu;
};

#pragma pack()


/* 
 * NOTE:  
 * 
 * Please note that EXPEDITED requests are never transmitted 
 * over the subnet interface but on a subnet to subnet level.
 * However we still define these here if we ever think of making
 * a Unix 5066 stack we'll already have the structures ;-).
 */

/* S_EXPEDITED_UNIDATA_REQUEST packet */
#pragma pack(1)
struct expedited_unidata_req {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int not_used:4;
	unsigned int sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int sap_id:4;
	unsigned int not_used:4;
#endif
	unsigned char addr_fld[4];
	/* struct service_type service_type; */
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int tx_mode:4;
	unsigned int dlvry_cnfrm:2;
	unsigned int dlvry_ordr:1;
	unsigned int ext_fld:1;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int ext_fld:1;
	unsigned int dlvry_ordr:1;
	unsigned int dlvry_cnfrm:2;
	unsigned int tx_mode:4;
#endif
	/* 
	 * dirty-part(tm)
	 */
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int no_retxs:4;
	unsigned int ttlmsb:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int ttlmsb:4;
	unsigned int no_retxs:4;
#endif
	unsigned int ttl:16;
	/* */
	unsigned int size_of_pdu:16;
};

#pragma pack()



/* S_EXPEDITED_UNIDATA_INDICATION packet */
#pragma pack(1)
struct expedited_unidata_ind {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int not_used:4;
	unsigned int dest_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int dest_sap_id:4;
	unsigned int not_used:4;
#endif
	unsigned char dest_node[4];
/* 
 * Transmission types used in expedited_unidata 
 * are the same as the unidata_indication 
 */
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int tx_mode:4;
	unsigned int source_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int source_sap_id:4;
	unsigned int tx_mode:4;
#endif
	unsigned char source_node[4];
	unsigned short size_of_u_pdu;
};

#pragma pack()



/* S_EXPEDITED_UNIDATA_CONFIRM packet */
#pragma pack(1)
struct expedited_unidata_req_confirm {
	unsigned char type;
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int not_used:4;
	unsigned int dest_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int dest_sap_id:4;
	unsigned int not_used:4;
#endif
	unsigned char dest_node[4];
	unsigned short size_of_u_pdu;
};

#pragma pack()



/* S_EXPEDITED_UNIDATA_REJECTED packet */
#pragma pack(1)
struct expedited_unidata_req_rejected {
	unsigned char type;
/* Reasons are the same as the S_UNIDATA_REJECTED primitive */
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int reason:4;
	unsigned int dest_sap_id:4;
#elif BYTE_ORDER == LITTLE_ENDIAN
	unsigned int dest_sap_id:4;
	unsigned int reason:4;
#endif
	unsigned char dest_node[4];
	unsigned short size_of_u_pdu;
};

#pragma pack()





/*****************************************/
/******* GENERIC S_PRIM FIELDS ***********/
/*****************************************/

/* Total length of S_primitive macro "-1" for the type field */
#define SPRIM_TLEN(sprim) ((sizeof(struct naked_s_hdr) + sizeof(struct sprim) - 1))


/* "Naked" "S_" primitive header (without primitives) */
#pragma pack(1)
struct naked_s_hdr {
	unsigned short preamble;
#if !defined(S5066_V1_0)
	unsigned char version;
#endif
	unsigned short sprim_len;
	unsigned char type;                                                       /*
     * Parses (and prints out) the SIS (Subnet-Interface Sub-Layer)
     * primitives.
     */
};

#pragma pack()



/* 
 * "S_" primitive Generic Elements and Format 
 */
#pragma pack(1)
struct s_hdr {
	unsigned short preamble;
#if !defined(S5066_V1_0)
	unsigned char version;
#endif
	unsigned short sprim_len;
	union {
		/* Binds */
		struct bind_request bind_request;
		struct unbind_request unbind_request;
		struct bind_accepted bind_accepted;
		struct bind_rejected bind_rejected;
		struct unbind_indication unbind_indication;
		/* Hard links */
		struct hard_link_establish hard_link_establish;
		struct hard_link_terminate hard_link_terminate;
		struct hard_link_established hard_link_established;
		struct hard_link_rejected hard_link_rejected;
		struct hard_link_terminated hard_link_terminated;
		struct hard_link_indication hard_link_indication;
		struct hard_link_accept hard_link_accept;
		struct hard_link_reject hard_link_reject;
		/* Subnet Availability */
		struct subnet_availability subnet_availability;
		/* Data flow */
		struct data_flow_on data_flow_on;
		struct data_flow_off data_flow_off;
		/* Keep alive */
		struct keep_alive keep_alive;
		/* Management */
		struct management_message_request
		 management_message_request;
		struct management_message_indication
		 management_message_indication;
		/* Unidata pkts */
		struct unidata_req_confirm unidata_req_confirm;
		struct unidata_ind unidata_ind;
		struct unidata_req unidata_req;
		struct unidata_req_rejected unidata_req_rejected;
		/* Expedited unidata pkts */
		struct expedited_unidata_req_confirm
		 expedited_unidata_req_confirm;
		struct expedited_unidata_ind expedited_unidata_ind;
		struct expedited_unidata_req expedited_unidata_req;
		struct expedited_unidata_req_rejected
		 expedited_unidata_req_rejected;
	} sprim;
};

#pragma pack()


/* End of header file */
#endif
