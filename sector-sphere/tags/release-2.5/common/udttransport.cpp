/*****************************************************************************
Copyright 2005 - 2010 The Board of Trustees of the University of Illinois.

Licensed under the Apache License, Version 2.0 (the "License"); you may not
use this file except in compliance with the License. You may obtain a copy of
the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
License for the specific language governing permissions and limitations under
the License.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 08/19/2010
*****************************************************************************/


#ifndef WIN32
   #include <sys/types.h>
   #include <sys/socket.h>
   #include <arpa/inet.h>
#else
   #include <windows.h>
#endif

#include <fstream>
#include <cstring>
#include "udttransport.h"

using namespace std;

UDTTransport::UDTTransport()
{
}

UDTTransport::~UDTTransport()
{
}

void UDTTransport::initialize()
{
   UDT::startup();
}

void UDTTransport::release()
{
   UDT::cleanup();
}

int UDTTransport::open(int& port, bool rendezvous, bool reuseaddr)
{
   m_Socket = UDT::socket(AF_INET, SOCK_STREAM, 0);

   if (UDT::INVALID_SOCK == m_Socket)
      return -1;

   UDT::setsockopt(m_Socket, 0, UDT_REUSEADDR, &reuseaddr, sizeof(bool));

   sockaddr_in my_addr;
   my_addr.sin_family = AF_INET;
   my_addr.sin_port = htons(port);
   my_addr.sin_addr.s_addr = INADDR_ANY;
   memset(&(my_addr.sin_zero), '\0', 8);

   if (UDT::bind(m_Socket, (sockaddr*)&my_addr, sizeof(my_addr)) == UDT::ERROR)
      return -1;

   int size = sizeof(sockaddr_in);
   UDT::getsockname(m_Socket, (sockaddr*)&my_addr, &size);
   port = ntohs(my_addr.sin_port);

   #ifdef WIN32
      int mtu = 1052;
      UDT::setsockopt(m_Socket, 0, UDT_MSS, &mtu, sizeof(int));
   #endif

   UDT::setsockopt(m_Socket, 0, UDT_RENDEZVOUS, &rendezvous, sizeof(bool));

   return 1;
}

int UDTTransport::listen()
{
   return UDT::listen(m_Socket, 1024);
}

int UDTTransport::accept(UDTTransport& t, sockaddr* addr, int* addrlen)
{
   timeval tv;
   UDT::UDSET readfds;

   tv.tv_sec = 0;
   tv.tv_usec = 10000;

   UD_ZERO(&readfds);
   UD_SET(m_Socket, &readfds);

   int res = UDT::select(1, &readfds, NULL, NULL, &tv);

   if ((res == UDT::ERROR) || (!UD_ISSET(m_Socket, &readfds)))
      return -1;

   t.m_Socket = UDT::accept(m_Socket, addr, addrlen);

   if (t.m_Socket == UDT::INVALID_SOCK)
      return -1;

   return 0;
}

int UDTTransport::connect(const char* ip, int port)
{
   sockaddr_in serv_addr;
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_port = htons(port);
   #ifndef WIN32
      inet_pton(AF_INET, ip, &serv_addr.sin_addr);
   #else
      serv_addr.sin_addr.s_addr = inet_addr(ip);
   #endif
      memset(&(serv_addr.sin_zero), '\0', 8);

   if (UDT::ERROR == UDT::connect(m_Socket, (sockaddr*)&serv_addr, sizeof(serv_addr)))
      return -1;

   return 1;
}

int UDTTransport::send(const char* buf, int size)
{
   int ssize = 0;
   while (ssize < size)
   {
      int ss = UDT::send(m_Socket, buf + ssize, size - ssize, 0);
      if (UDT::ERROR == ss)
         return -1;

      ssize += ss;
   }

   return ssize;
}

int UDTTransport::recv(char* buf, int size)
{
   int rsize = 0;
   while (rsize < size)
   {
      int rs = UDT::recv(m_Socket, buf + rsize, size - rsize, 0);
      if (UDT::ERROR == rs)
         return -1;

      rsize += rs;
   }

   return rsize;
}

int64_t UDTTransport::sendfile(fstream& ifs, int64_t offset, int64_t size)
{
   return UDT::sendfile(m_Socket, ifs, offset, size);
}

int64_t UDTTransport::recvfile(fstream& ifs, int64_t offset, int64_t size)
{
   return UDT::recvfile(m_Socket, ifs, offset, size);
}

int UDTTransport::close()
{
   return UDT::close(m_Socket);
}

bool UDTTransport::isConnected()
{
   return (UDT::send(m_Socket, NULL, 0, 0) == 0);
}

int64_t UDTTransport::getRealSndSpeed()
{
   UDT::TRACEINFO perf;
   if (UDT::perfmon(m_Socket, &perf) < 0)
      return -1;

   if (perf.usSndDuration <= 0)
      return -1;

   int mss;
   int size = sizeof(int);
   UDT::getsockopt(m_Socket, 0, UDT_MSS, &mss, &size);
   return int64_t(8.0 * perf.pktSent * mss / (perf.usSndDuration / 1000000.0));
}

int UDTTransport::getsockname(sockaddr* addr)
{
   int size = sizeof(sockaddr_in);
   return UDT::getsockname(m_Socket, addr, &size);
}

int UDTTransport::initCoder(unsigned char key[16], unsigned char iv[16])
{
   m_Encoder.initEnc(key, iv);
   m_Decoder.initDec(key, iv);
   return 0;
}

int UDTTransport::releaseCoder()
{
   m_Encoder.release();
   m_Decoder.release();
   return 0;
}

int UDTTransport::secure_send(const char* buf, int size)
{
   char* tmp = new char[size + 64];
   int len = size + 64;
   m_Encoder.encrypt((unsigned char*)buf, size, (unsigned char*)tmp, len);

   send((char*)&len, 4);
   send(tmp, len);
   delete [] tmp;

   return size;
}

int UDTTransport::secure_recv(char* buf, int size)
{
   int len;
   if (recv((char*)&len, 4) < 0)
      return -1;

   char* tmp = new char[len];
   if (recv(tmp, len) < 0)
   {
      delete [] tmp;
      return -1;
   }

   m_Decoder.decrypt((unsigned char*)tmp, len, (unsigned char*)buf, size);

   delete [] tmp;

   return size;
}

int64_t UDTTransport::secure_sendfile(fstream& ifs, int64_t offset, int64_t size)
{
   const int block = 640000;
   char* tmp = new char[block];

   ifs.seekg(offset);

   int64_t tosend = size;
   while (tosend > 0)
   {
      int unitsize = int((tosend < block) ? tosend : block);
      ifs.read(tmp, unitsize);
      if (secure_send(tmp, unitsize) < 0)
         break;
      tosend -= unitsize;
   }

   delete [] tmp;
   return size - tosend;
}

int64_t UDTTransport::secure_recvfile(fstream& ofs, int64_t offset, int64_t size)
{
   const int block = 640000;
   char* tmp = new char[block];

   ofs.seekp(offset);

   int64_t torecv = size;
   while (torecv > 0)
   {
      int unitsize = int((torecv < block) ? torecv : block);
      if (secure_recv(tmp, unitsize) < 0)
         break;
      ofs.write(tmp, unitsize);
      torecv -= unitsize;
   }

   delete [] tmp;
   return size - torecv;
}

int UDTTransport::sendEx(const char* buf, int size, bool secure)
{
   if (!secure)
      return send(buf, size);
   return secure_send(buf, size);
}

int UDTTransport::recvEx(char* buf, int size, bool secure)
{
   if (!secure)
      return recv(buf, size);
   return secure_recv(buf, size);
}

int64_t UDTTransport::sendfileEx(fstream& ifs, int64_t offset, int64_t size, bool secure)
{
   if (!secure)
      return sendfile(ifs, offset, size);
   return secure_sendfile(ifs, offset, size);
}

int64_t UDTTransport::recvfileEx(fstream& ofs, int64_t offset, int64_t size, bool secure)
{
   if (!secure)
      return recvfile(ofs, offset, size);
   return secure_recvfile(ofs, offset, size);
}
