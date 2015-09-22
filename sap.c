/*****************************************************************************
 * sap.c SAP Announcements generated from SDT
 *****************************************************************************
 * Copyright (C) 2012 VideoLAN
 *
 * Author:  Dirk Braunschweiger <dirkmb@selfnet.de>
 *          Markus Wick         <markus@selfnet.de>
 *          Tillmann Karras     <tillmann@selfnet.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>

#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <locale.h>
#include <errno.h>

#include <bitstream/dvb/si/sdt.h>
#include <bitstream/dvb/si/eit.h>
#include <bitstream/dvb/si/desc_48.h>
#include <bitstream/dvb/si/desc_4d.h>

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#include "dvblast.h"
#include "sap.h"

/* SAP config */
unsigned int g_sap_interval = 1;
in_addr_t  g_sap_ip4_dest = -1;
struct in6_addr g_sap_ip6_dest = { .s6_addr = { 0 } };

/* Reporting timer */
static mtime_t sap_time = 0;

static int i_sap_handle;
static int i_next_output = 0;
static struct sockaddr_in  addr4;
static struct sockaddr_in6 addr6;

/* Checks if the UTF-8 character pointed to by chr is a DVB control code
 * as defined by EN 300 468 Annex A.1. */
static inline int dvb_is_control_code( char *chr, int length )
{
    /* c2 80-9f  or  ee 82 80-9f */
    return ( length == 2 && chr[0] == '\xc2' && (chr[1] & 0xe0) == 0x80 ) ||
           ( length == 3 && chr[0] == '\xee' && chr[1] == '\x82' && (chr[2] & 0xe0) == 0x80 );
}

/* Strips DVB control codes in-place and returns the new length.
 * Probably not very efficient and only supports UTF-8. */
static size_t dvb_string_strip_control_codes( char *str, size_t size )
{
    char *src = str, *dst = str;
    size_t remaining = size;

    if ( strcmp( psz_native_charset, "UTF-8" ) != 0 )
    {
        return size;
    }

    while ( remaining )
    {
        int char_length = mblen( src, remaining );
        if ( char_length == 0 )
            break;
        if ( char_length == -1 )
        {
            msg_Warn( NULL, "Invalid UTF-8 in DVB string\n" );
            break;
        }
        if ( !dvb_is_control_code( src, char_length ) )
        {
            // probably more efficient than memmove()
            switch ( char_length )
            {
            case 4:
                *dst++ = *src++;
            case 3:
                *dst++ = *src++;
            case 2:
                *dst++ = *src++;
            case 1:
                *dst++ = *src++;
            }
        } else {
            src += char_length;
        }
        remaining -= char_length;
    }
    return dst - str;
}

/* Converts a DVB string into native encoding and returns its new size. */
static size_t dvb_string_copy( char *dest, size_t dest_max_len,
                               const uint8_t *src, size_t src_len )
{
    if ( !src_len || !dest_max_len )
        return 0;

    char *dest_orig = dest;
#ifdef HAVE_ICONV
    const char *psz_encoding = dvb_string_get_encoding(&src, &src_len);

    if ( !psz_encoding )
        psz_encoding = psz_dvb_charset;

    iconv_t p_iconv = iconv_open(psz_native_charset, psz_encoding);

    size_t p_remain[2] = {src_len, dest_max_len};
    iconv(p_iconv, (char**)&src, p_remain,
            &dest, p_remain+1);

    iconv_close(p_iconv);

    size_t len = dest - dest_orig;
#else
    size_t len = dest_max_len < src_len ? dest_max_len : src_len;
    memcpy(dest, src, len);
#endif

    dest_orig[len] = '\0';

    len = dvb_string_strip_control_codes( dest_orig, len );

    return len;
}

void sap_Init(void)
{
    /* required for mblen() */
    setlocale (LC_ALL, "");

    if ( g_sap_ip4_dest == -1 )
        inet_pton(AF_INET, SAP_DEFAULT_IP4_ADDR, &g_sap_ip4_dest);

    if ( g_sap_ip6_dest.s6_addr[0] == 0 )
        inet_pton(AF_INET6, SAP_DEFAULT_IP6_ADDR, &g_sap_ip6_dest);

    /* IPv4 */
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_addr.s_addr = g_sap_ip4_dest;
    addr4.sin_port = htons(SAP_DPORT);
    addr4.sin_family = AF_INET;

    /* IPv6 */
    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_addr = g_sap_ip6_dest;
    addr6.sin6_port = htons(SAP_DPORT);
    addr6.sin6_family = AF_INET6;

    /* setting starttime */
    sap_time = i_wallclock;
}

void sap_Announce(void)
{
    /* no output, so no announcements */
    if ( i_nb_outputs <= 0 ) return;

    /* See if we need to send the announcements */
    if ( i_wallclock < sap_time ) return;

    /* Set the timer for next time
     *
     * Normally we add the interval to the previous time so that if one
     * dump is a bit late, the next one still occurs at the correct time.
     * However, if there is a long gap (e.g. because the channel has
     * stopped for some time), then just rebase the timing to the current
     * time.  I've chosen SAP_INTERVAL as the long gap - this is arbitary */
    if ( i_wallclock > sap_time + g_sap_interval * 1000000ll )
    {
       msg_Dbg(NULL, "SAP is %ld seconds late - reset timing\n", (i_wallclock - sap_time) / 1000000);
       sap_time = i_wallclock;
    }
    sap_time += g_sap_interval*1000000ll/i_nb_outputs;

    /* switching to the next output stream */
    if ( ++i_next_output >= i_nb_outputs ) i_next_output = 0;
    output_t *p_output = pp_outputs[i_next_output];

    char psz_session_addr[INET6_ADDRSTRLEN] = ""; /* IP of session (i.e. not the SAP stream!) */
    char psz_session_port[6] = "";                /* port of session */
    int i_mtu = p_output->config.i_mtu;
    int i_fam = p_output->config.i_family;
    struct sockaddr_storage local_addr;
    socklen_t i_local_addr_len = sizeof(local_addr);
    char psz_fqdn[NI_MAXHOST] = "";
    struct addrinfo *ai_list, *ai, hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_DGRAM };

    /* Get multicast group address of the DVB stream to be announced */
    getnameinfo( (struct sockaddr*) &p_output->config.connect_addr, sizeof(struct sockaddr_storage),
                 psz_session_addr, sizeof(psz_session_addr),
                 psz_session_port, sizeof(psz_session_port),
                 NI_NUMERICHOST | NI_NUMERICSERV );

    /* Get local address of the DVB streaming socket */
    getsockname( p_output->i_handle, (struct sockaddr*) &local_addr, &i_local_addr_len );

    /* Get local FQDN (or IP as string) */
    gethostname( psz_fqdn, sizeof(psz_fqdn) );
    getaddrinfo( psz_fqdn, NULL, &hints, &ai_list );
    for ( ai = ai_list; ai; ai = ai->ai_next )
    {
        if ( !getnameinfo( ai->ai_addr, ai->ai_addrlen, psz_fqdn, sizeof(psz_fqdn), NULL, 0, 0 ) )
        {
            /* success */
            break;
        }
    }
    if ( !ai )
    {
        msg_Err( NULL, "Are you even connected to a network?" );
        exit( EXIT_FAILURE );
    }
    freeaddrinfo( ai_list );

    uint16_t i_sid = 0;            /* service id of this stream */
    const uint8_t *p_service = 0;  /* name of service */
    uint8_t i_service_len = 0;
    uint8_t i_service_type = 0;    /* type of service (see ETSI EN 300 468 table 86) */
    const uint8_t *p_event = 0;    /* name of event */
    uint8_t i_event_len = 0;
    const uint8_t *p_text = 0;     /* long description of event */
    uint8_t i_text_len = 0;

    /* Parsing SDT */
    int j = 0, k = 0;
    uint8_t *p_sdt = p_output->p_sdt_section;
    if ( p_sdt )
    {
        uint8_t *sdtn;
        while ( (sdtn = sdt_get_service( p_sdt, k++ )) != NULL )
        {
            i_sid = sdtn_get_sid( sdtn );

            /* searching for 0x48 service descriptor */
            uint8_t *p_desc;
            while ( (p_desc = descs_get_desc( sdtn_get_descs( sdtn ), j++ )) != NULL )
            {
                if ( desc_get_tag( p_desc ) == 0x48 && desc48_validate( p_desc ) )
                {
                    p_service = desc48_get_service( p_desc, &i_service_len );
                    i_service_type = desc48_get_type( p_desc );
                }
            }
        }
    } else {
        msg_Dbg( NULL, "not announcing SAP until we get a SDT" );
        return;
    }

    /* Parsing EIT_EPG */
    j = 0, k = 0;
    uint8_t *p_eit = p_output->p_eit_epg_section;
    if ( p_eit )
    {
        uint8_t *eitn;
        while ( (eitn = eit_get_event(p_eit, k++)) != NULL )
        {
            uint8_t *p_desc;
            while ( (p_desc = descs_get_desc( eitn_get_descs(eitn), j++ )) != NULL )
            {
                if ( desc_get_tag( p_desc ) == 0x4d && desc4d_validate( p_desc ) )
                {
                    p_event = desc4d_get_event_name( p_desc, &i_event_len );
                    p_text = desc4d_get_text( p_desc, &i_text_len );
                }
            }
        }
    }

    /* constructing the SAP/SDP package */
    char buffer[i_mtu];
    char *worker = buffer;
    char *worker_end = buffer+i_mtu;
    int addr_len = i_fam == AF_INET6 ? 16 : 4;

    /* create the SAP header
     *
     * Byte 0: V version number v1&v2     = 001      (3 bits)
     *         A address type   IPv4/IPv6 = 0/1      (1 bit)
     *         R reserved                 = 0        (1 bit)
     *         T message type   ann/del   = 0/1      (1 bit)
     *         E encryption     off/on    = 0/1      (1 bit)
     *         C compressed     off/on    = 0/1      (1 bit)
     */

    /*            VVVARTEC  (announces only, not encrypted, not compressed) */
    worker[0] = 0b00100000;
    worker[0] |= !!(i_fam == AF_INET6) << 4;

    /* Byte 1 : Authentification length - Not supported */
    worker[1] = 0x00;

    /* Bytes 2-3 : Message Id Hash */
    worker[2] = (i_sid>>8) & 0xff;
    worker[3] = i_sid & 0xff;
    worker += 4;

    /* Bytes 4-7 (or 4-19) byte: Originating source */
    if ( i_fam == AF_INET6 )
        memcpy(worker, &((struct sockaddr_in6*) &local_addr)->sin6_addr, addr_len);
    else
        memcpy(worker, &((struct sockaddr_in*) &local_addr)->sin_addr, addr_len);
    worker += addr_len;

    /* finally MIME type */
    worker += snprintf(worker, worker_end-worker, "application/sdp");
    worker[0] = 0;
    worker++;

    /* and the SDP payload */
    worker += snprintf(worker, worker_end-worker,
                       "v=0\r\n"
                       "o=- %d 1 IN %s %s\r\n"
                       "s=", i_sid, i_fam == AF_INET6 ? "IP6" : "IP4", psz_fqdn);
    worker += dvb_string_copy(worker, worker_end-worker, p_service, i_service_len);
    if ( i_event_len )
    {
        worker += snprintf(worker, worker_end-worker, " [");
        worker += dvb_string_copy(worker, worker_end-worker, p_event, i_event_len);
        worker += snprintf(worker, worker_end-worker, "]");
    }
    worker += snprintf(worker, worker_end-worker,
                       "\r\n"
                       "i=");
    worker += dvb_string_copy(worker, worker_end-worker, p_text, i_text_len);
    worker += snprintf(worker, worker_end-worker,
                       "\r\n"
                       "u=http://www.videolan.org/projects/dvblast.html\r\n");
    if ( i_fam == AF_INET6 )
    {
        worker += snprintf(worker, worker_end-worker,
                       "c=IN IP6 %s\r\n",
                       psz_session_addr);
    }
    else
    {
        worker += snprintf(worker, worker_end-worker,
                       "c=IN IP4 %s/%d\r\n",
                       psz_session_addr, p_output->config.i_ttl);
    }
    const char *service_type;
    switch (i_service_type)
    {
    case 1:  service_type = "Television"; break;
    case 2:  service_type = "Radio";      break;
    default: service_type = "Unknown";    break;
    }
    worker += snprintf(worker, worker_end-worker,
                       "t=0 0\r\n"
                       "a=tool:dvblast %s (%s)\r\n"
                       "a=type:broadcast\r\n" /* implies a=recvonly */
                       "a=charset:%s\r\n"
                       /* now the media-level: */
                       "m=video %s RTP/AVP 33\r\n" /* FIXME: other parts of dvblast also support sending raw UDP */
//                       "i=Media Title\r\n" // maybe put the description here?
                       "a=cat:%s\r\n"
//                       "a=rtpmap:33 MP2T/90000\r\n" // we don't need this because 33 is a standardized RTP type
//                       "a=lang:de\r\n"
                       , VERSION, VERSION_EXTRA, psz_native_charset, psz_session_port, service_type);

    /* sending this announcement */
    if ( (i_fam == AF_INET  && sendto( p_output->i_handle, buffer, worker-buffer, 0,
                (struct sockaddr*) &addr4, sizeof(addr4)) < 0) ||
         (i_fam == AF_INET6 && sendto( p_output->i_handle, buffer, worker-buffer, 0,
                (struct sockaddr*) &addr6, sizeof(addr6)) < 0 ))
    {
        msg_Err( NULL, "couldn't send to %d (%s)",
                 i_sap_handle, strerror(errno) );
    }
}

void sap_Close(void)
{
    if ( i_sap_handle >= 0 )
	close( i_sap_handle );
}
