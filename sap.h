/*****************************************************************************
 * sap.h SAP Announcements generated from SDT
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

#ifndef SAP_H
#define SAP_H

// Define the dump period in seconds
#define SAP_INTERVAL   1
#define SAP_IPv4_DEST  "239.195.255.255"
#define SAP_IPv4_DPORT 9875
#define SAP_IPv4_TTL   255


void sap_Init(void);
void sap_Announce(void);
void sap_Close(void);

#endif
