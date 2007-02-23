/*****************************************************************************
Copyright � 2006, 2007, The Board of Trustees of the University of Illinois.
All Rights Reserved.

Group Messaging Protocol (GMP)

National Center for Data Mining (NCDM)
University of Illinois at Chicago
http://www.ncdm.uic.edu/

This library is free software; you can redistribute it and/or modify it
under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or (at
your option) any later version.

This library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser
General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu [gu@lac.uic.edu], last updated 02/13/2007
*****************************************************************************/


#ifndef __GMP_H__
#define __GMP_H__

#ifndef WIN32
   #include <pthread.h>
   #include <sys/time.h>
   #include <sys/types.h>
   #include <sys/socket.h>
   #include <sys/stat.h>
   #include <netinet/in.h>
   #include <arpa/inet.h>

   #define GMP_API
#else
   #include <windows.h>

   #ifdef GMP_EXPORTS
      #define GMP_API __declspec(dllexport)
   #else
      #define GMP_API __declspec(dllimport)
   #endif
#endif

#include <util.h>
#include <message.h>
#include <prec.h>

#include <vector>
#include <string>
#include <map>
#include <set>
#include <list>
#include <queue>
using namespace std;


class CGMPMessage
{
public:
   CGMPMessage();
   ~CGMPMessage();

   int32_t& m_iType;		// 0 Data; 1 ACK
   int32_t& m_iSession;
   int32_t& m_iID;		// message ID
   int32_t& m_iInfo;		//

   char* m_pcData;
   int m_iLength;

   int32_t m_piHeader[4];

public:
   void pack(const char* data, const int& len, const int32_t& info = 0);
   void pack(const int32_t& type, const int32_t& info = 0);

public:
   static int32_t g_iSession;

private:
   static int32_t initSession();

   static int32_t g_iID;
   static pthread_mutex_t g_IDLock;

   static const int32_t g_iMaxID = 0xFFFFFFF;

   static const int m_iHdrSize = 16;
};

struct CMsgRecord
{
   char m_pcIP[64];
   int m_iPort;
   CGMPMessage* m_pMsg;
   int64_t m_llTimeStamp;
};

struct CFMsgRec
{
   bool operator()(const CMsgRecord* m1, const CMsgRecord* m2) const
   {
      if (strcmp(m1->m_pcIP, m2->m_pcIP) == 0)
      {
         if (m1->m_iPort == m2->m_iPort)
            return m1->m_pMsg->m_iID > m2->m_pMsg->m_iID;

         return (m1->m_iPort > m2->m_iPort);
      }
      
      return strcmp(m1->m_pcIP, m2->m_pcIP);
   }
};


class GMP_API CGMP
{
public:
   CGMP();
   ~CGMP();

public:
   int init(const int& port = 0);
   int close();

public:
   int sendto(const char* ip, const int& port, int32_t& id, const char* data, const int& len, const bool& reliable = true);
   int recvfrom(char* ip, int& port, int32_t& id, char* data, int& len);
   int recv(const int32_t& id, char* data, int& len);

private:
   int UDPsend(const char* ip, const int& port, int32_t& id, const char* data, const int& len, const bool& reliable = true);
   int TCPsend(const char* ip, const int& port, int32_t& id, const char* data, const int& len);

public:
   int sendto(const char* ip, const int& port, int32_t& id, const CUserMessage* msg);
   int recvfrom(char* ip, int& port, int32_t& id, CUserMessage* msg);
   int recv(const int32_t& id, CUserMessage* msg);
   int rpc(const char* ip, const int& port, CUserMessage* req, CUserMessage* res);

   int rtt(const char* ip, const int& port);

private:
   pthread_t m_SndThread;
   pthread_t m_RcvThread;
   pthread_t m_TCPRcvThread;
#ifndef WIN32
   static void* sndHandler(void*);
   static void* rcvHandler(void*);
   static void* tcpRcvHandler(void*);
#else
   static DWORD WINAPI sndHandler(LPVOID);
   static DWORD WINAPI rcvHandler(LPVOID);
   static DWORD WINAPI tcpRcvHandler(LPVOID);
#endif

   pthread_mutex_t m_SndQueueLock;
   pthread_cond_t m_SndQueueCond;
   pthread_mutex_t m_RcvQueueLock;
   pthread_cond_t m_RcvQueueCond;
   pthread_mutex_t m_ResQueueLock;
   pthread_cond_t m_ResQueueCond;
   pthread_mutex_t m_RTTLock;
   pthread_cond_t m_RTTCond;

private:
   int m_iPort;

   #ifndef WIN32
      int m_UDPSocket;
      int m_TCPSocket;
   #else
      SOCKET m_UDPSocket;
      SOCKET m_TCPSocket;
   #endif

   list<CMsgRecord*> m_lSndQueue;
   queue<CMsgRecord*> m_qRcvQueue;
   map<int32_t, CMsgRecord*> m_mResQueue;
   CPeerManagement m_PeerHistory;

   volatile bool m_bInit;
   volatile bool m_bClosed;

private:
   static const int m_iMaxUDPMsgSize = 1456;
};

#endif
