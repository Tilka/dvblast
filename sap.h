/*****************************************************************************
 * sap.h SAP Announcements generated from SDT
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

#ifndef SAP_H
#define SAP_H

// Define the dump period in seconds
extern unsigned int g_sap_interval;

/* The IP multicast addresses to be used for SAP announcements.
 * See RFC 2974 section 3 and RFC 2365 sections 6-7.
 *
 * IPv4: always the highest address of the admin scope
 * IPv6: always ff0x::2:7ffe where x denotes the admin scope
 */
extern in_addr_t g_sap_ip4_dest;
extern struct in6_addr g_sap_ip6_dest;

#define SAP_DEFAULT_IP4_ADDR "239.195.255.255"
#define SAP_DEFAULT_IP6_ADDR "ff08::2:7ffe"
#define SAP_DPORT 9875

void sap_Init(void);
void sap_Announce(void);
void sap_Close(void);

#endif
