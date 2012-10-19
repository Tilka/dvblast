/*****************************************************************************
 * sap.c SAP Announcements generated from SDT
 *****************************************************************************
 * Copyright Tripleplay service 2004,2005,2011
 *
 * Author:  Dirk Braunschweiger <dirkmb@selfnet.de>
 *          Markus Wick         <markus@selfnet.de>
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>

#include <bitstream/dvb/si/sdt.h>

#include "dvblast.h"
#include "sap.h"

// Reporting timer
#if defined( WIN32 )
static LARGE_INTEGER sap_time;
static LARGE_INTEGER sap_inc;
#else
static struct timeval sap_time = { 0, 0 };
#endif


int i_sap_handle;
int i_next_output;
struct sockaddr_in addr;

void sap_Init(void)
{
//    socklen_t i_sockaddr_len = (p_config->i_family == AF_INET) ?
//                               sizeof(struct sockaddr_in) :
//                               sizeof(struct sockaddr_in6);

// No IPv6 Support, yet TODO

    i_sap_handle = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

    if ( i_sap_handle < 0 )
    {
        msg_Err( NULL, "couldn't create socket (%s)", strerror(errno) );
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_addr.s_addr = inet_addr(SAP_IPv4_DEST);
    addr.sin_port = htons(SAP_IPv4_DPORT);
    addr.sin_family = AF_INET;


    unsigned char ttl = SAP_IPv4_TTL;
    setsockopt(i_sap_handle, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    i_next_output = 0;
}

void sap_Announce(void)
{   
    // no output, so no announcements
    if(i_nb_outputs <= 0) return;

    // See if we need to send the announcements
    struct timeval now, delta, diff;
    gettimeofday(&now, NULL);
    if (timercmp(&now, &sap_time, <)) return;

    // Set the timer for next time
    //
    // Normally we add the interval to the previous time so that if one
    // dump is a bit late, the next one still occurs at the correct time.
    // However, if there is a long gap (e.g. because the channel has
    // stopped for some time), then just rebase the timing to the current
    // time.  I've chosen SAP_INTERVAL as the long gap - this is arbitary
    delta.tv_sec = SAP_INTERVAL/i_nb_outputs;
    delta.tv_usec = SAP_INTERVAL*1000000/i_nb_outputs%1000000;
    
    timersub(&now, &sap_time, &diff);
    if (timercmp(&diff,&delta,>)) {
        msg_Dbg(NULL, "SAP is %ld seconds late - reset timing\n", diff.tv_sec);
        sap_time = now;
    }
    timeradd(&sap_time, &delta, &sap_time);

    if(++i_next_output >= i_nb_outputs) i_next_output = 0;
           
    char host[100] = "";
    char serv[100] = "";
    char service_provider[500] = "";
    char service_name[500] = "";

    // fetch ip / port combo
    getnameinfo((struct sockaddr*)&pp_outputs[i_next_output]->config.connect_addr, 
                sizeof(struct sockaddr_storage), host, 100, serv, 100, 
                NI_NUMERICHOST | NI_NUMERICSERV);

    // Parsing SDT
    uint8_t *sdt = pp_outputs[i_next_output]->p_sdt_section;
    if(!sdt) return;

    uint16_t sid = sdt[11]<<8 | sdt[12]; // SID at byte [11,12]

    int j,k;
    k=0;                                 // provider begins at 20
    for(j=0; j<sdt[19] && j<499; j++)    // end is stored in sdt[19]
        if(sdt[j+20] >= 0x20)
            service_provider[k++] = sdt[j+20];
    service_provider[k] = 0;
        
    k=0;                                 // service begins 1 byte after provider
    for(j=0; j<sdt[20+sdt[19]] && j<499; j++)
        if(sdt[21+sdt[19]+j] >= 0x20)
            service_name[k++] = sdt[21+sdt[19]+j];
    service_name[k] = 0;

    char buffer[1500];
    char *worker = buffer;
    char *worker_end = buffer+1500;

    // create the sap header
    worker[0] = 0x20;
    worker[1] = 0x00;
    worker[2] = (sid>>8) & 0xff;
    worker[3] = sid & 0xff;
    worker+=4;

    worker += snprintf(worker, worker_end-worker, "\x01\x02\x03\x05");
    worker += snprintf(worker, worker_end-worker, "application/sdp");
    worker[0] = 0;
    worker++;

    // and the sdp payload
    worker += snprintf(worker, worker_end-worker, 
                       "v=0\r\n"
                       "o=VideoLan 16975 1 IN IP4 www.videolan.org\r\n"
                       "s=%s\r\n"
                       "i=%s\r\n"
                       "u=http://www.videolan.org/projects/dvblast.html\r\n"
                       "c=IN IP4 %s/255\r\n"
                       "t=0 0\r\n"
                       "a=tool:dvblast 2.2-sap-patched\r\n"
                       "a=type:broadcast\r\n"
                       "a=charset:UTF-8\r\n"
                       "m=video %s RTP/AVP 33\r\n"
                       "a=rtpmap:33 MP2T/90000\r\n"
                       "a=rtcp:1235\r\n"
//                       "a=lang:de\r\n"
                       , service_name, service_provider, host, serv);

    //for(j=0; j<60; j++) printf("%02x-", sdt[j]);
    //printf(" - rtp://@%s:%s %s %s - (%d) %ld bytes\n", 
    //       host, serv, service_provider, service_name, sid, worker-buffer);

    // sending this announcement
    if ( sendto(i_sap_handle , buffer , worker-buffer , 0 , 
                (struct sockaddr*)&addr , sizeof(addr)) < 0 )
    {   
        msg_Err( NULL, "couldn't send to %d (%s)",
                 i_sap_handle, strerror(errno) );
    }
}

void sap_Close(void)
{
    if(i_sap_handle >= 0)
	close( i_sap_handle );

}

