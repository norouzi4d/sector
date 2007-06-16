/*****************************************************************************
Copyright � 2006, 2007, The Board of Trustees of the University of Illinois.
All Rights Reserved.

National Center for Data Mining (NCDM)
University of Illinois at Chicago
http://www.ncdm.uic.edu/

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option)
any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51
Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu [gu@lac.uic.edu], last updated 02/23/2007
*****************************************************************************/


#ifndef __CB_TRANSPORT_H__
#define __CB_TRANSPORT_H__

#include <udt.h>

namespace cb
{

class Transport
{
public:
   Transport();
   ~Transport();

public:
   int open(int& port);
   int connect(const char* ip, const int& port);
   int send(const char* buf, const int& size);
   int recv(char* buf, const int& size);
   int sendfile(std::ifstream& ifs, const int64_t& offset, const int64_t& size);
   int recvfile(std::ofstream& ifs, const int64_t& offset, const int64_t& size);
   int close();

private:
   UDTSOCKET m_Socket;
};

};

#endif
