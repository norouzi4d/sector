/*****************************************************************************
Copyright (c) 2005 - 2010, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 12/31/2010
*****************************************************************************/

#ifndef WIN32
   #include <sys/time.h>
   #include <time.h>
#else
   #include <windows.h>
#endif

#include <iostream>

#include "common.h"
#include "dhash.h"
#include "prec.h"

using namespace std;

CPeerRecord::CPeerRecord():
m_strIP(),
m_iPort(0),
m_iSession(0),
m_iID(0),
m_llTimeStamp(0)
{
}

CPeerRecord& CPeerRecord::operator=(CPeerRecord& obj)
{
   m_strIP = obj.m_strIP;
   m_iPort = obj.m_iPort;
   m_iSession = obj.m_iSession;
   m_iID = obj.m_iID;
   m_llTimeStamp = obj.m_llTimeStamp;

   return *this;
}

bool CPeerRecord::operator==(CPeerRecord& obj)
{
   if (m_strIP != obj.m_strIP)
      return false;
   if (m_iPort != obj.m_iPort)
      return false;
   if (m_iSession != obj.m_iSession)
      return false;
   if (m_iID != obj.m_iID)
      return false;

   return true;
}

CPeerRecord* CPeerRecord::clone()
{
   CPeerRecord* pr = new CPeerRecord;
   *pr = *this;
   return pr;
}

int CPeerRecord::getKey()
{
   return CPeerMgmt::hash(m_strIP, m_iPort, m_iSession, m_iID);
};

CPeerRTT& CPeerRTT::operator=(CPeerRTT& obj)
{
   m_strIP = obj.m_strIP;
   m_iRTT = obj.m_iRTT;
   m_llTimeStamp = obj.m_llTimeStamp;

   return *this;   
}

bool CPeerRTT::operator==(CPeerRTT& obj)
{
   return m_strIP == obj.m_strIP;
}

CPeerRTT* CPeerRTT::clone()
{
   CPeerRTT* rtt = new CPeerRTT;
   *rtt = *this;
   return rtt;
}

int CPeerRTT::getKey()
{
   return CPeerMgmt::hash(m_strIP);
}

void CUDTConns::release()
{
   // Release openned socket, and acknowledge the peer side to
   // close its corresponding socket as well.
   UDT::close(m_UDT);
}

CUDTConns& CUDTConns::operator=(CUDTConns& obj)
{
   m_strIP = obj.m_strIP;
   m_iPort = obj.m_iPort;
   m_UDT = obj.m_UDT;

   return *this;
}

bool CUDTConns::operator==(CUDTConns& obj)
{
   if (m_strIP != obj.m_strIP)
      return false;
   if (m_iPort != obj.m_iPort)
      return false;
   return true;
}

CUDTConns* CUDTConns::clone()
{
   CUDTConns* conn = new CUDTConns;
   *conn = *this;
   return conn;
}

int CUDTConns::getKey()
{
   return CPeerMgmt::hash(m_strIP, m_iPort);
}

CFlowWindow& CFlowWindow::operator=(CFlowWindow& obj)
{
   m_strIP = obj.m_strIP;
   m_iPort = obj.m_iPort;
   m_iSession = obj.m_iSession;
   m_iWindowSize = obj.m_iWindowSize;
   m_llTimeStamp = obj.m_llTimeStamp;
   return *this;
}

bool CFlowWindow::operator==(CFlowWindow& obj)
{
  return ((m_strIP == obj.m_strIP) &&
          (m_iPort == obj.m_iPort) &&
          (m_iSession = obj.m_iSession));
}

CFlowWindow* CFlowWindow::clone()
{
   CFlowWindow* win = new CFlowWindow;
   *win = *this;
   return win;
}

int CFlowWindow::getKey()
{
   return CPeerMgmt::hash(m_strIP, m_iPort, m_iSession);
}

CPeerMgmt::CPeerMgmt()
{
   //TODO: this should be larger.
   m_RecentRec.setSizeLimit(m_uiRecLimit);
   m_PeerRTT.setSizeLimit(m_uiRecLimit);
   m_PersistentUDT.setSizeLimit(m_uiRecLimit);
   m_FlowWindow.setSizeLimit(m_uiRecLimit);

   CGuard::createMutex(m_PeerRecLock);
}

CPeerMgmt::~CPeerMgmt()
{
   // Release all cached items.
   clear();
   CGuard::releaseMutex(m_PeerRecLock);
}

void CPeerMgmt::insert(const string& ip, const int& port, const int& session, const int32_t& id)
{
   CGuard recguard(m_PeerRecLock);

   CPeerRecord pr;
   pr.m_strIP = ip;
   pr.m_iPort = port;
   pr.m_iSession = session;
   pr.m_iID = id;
   pr.m_llTimeStamp = CTimer::getTime();
   m_RecentRec.update(&pr);
}

int CPeerMgmt::setRTT(const string& ip, const int& rtt)
{
   CGuard recguard(m_PeerRecLock);

   if (rtt <= 0)
      return -1;

   CPeerRTT rtt_info;
   rtt_info.m_strIP = ip;
   rtt_info.m_iRTT = rtt;
   return m_PeerRTT.update(&rtt_info);
}

int CPeerMgmt::getRTT(const string& ip)
{
   CGuard recguard(m_PeerRecLock);
   CPeerRTT rtt_info;
   rtt_info.m_strIP = ip;
   if (m_PeerRTT.lookup(&rtt_info) == 0)
      return rtt_info.m_iRTT;
   return -1;
}

void CPeerMgmt::clearRTT(const string& ip)
{
   CGuard recguard(m_PeerRecLock);
   CPeerRTT rtt_info;
   rtt_info.m_strIP = ip;
   rtt_info.m_iRTT = -1;
   m_PeerRTT.update(&rtt_info);
}

int CPeerMgmt::setFlowWindow(const string& ip, const int& port, const int& session, const int& size)
{
   CGuard recguard(m_PeerRecLock);
   CFlowWindow window;
   window.m_strIP = ip;
   window.m_iPort = port;
   window.m_iSession = session;
   window.m_iWindowSize = size;
   return m_FlowWindow.update(&window);
}

int CPeerMgmt::flowControl(const string& ip, const int& port, const int& session)
{
   CGuard::enterCS(m_PeerRecLock);
   CFlowWindow window;
   window.m_strIP = ip;
   window.m_iPort = port;
   window.m_iSession = session;
   int ret = m_FlowWindow.lookup(&window);
   CGuard::leaveCS(m_PeerRecLock);

   if (ret != 0)
      return -1;

   // TODO: this flow control scheme is very naive. Need better solution.

   int thresh = window.m_iWindowSize - int((CTimer::getTime() - window.m_llTimeStamp) / 1000);

   if (thresh > 100)
   {
      #ifndef WIN32
         usleep(100000);
      #else
         Sleep(1);
      #endif
      return 100000;
   }

   if (thresh > 10)
   {
      #ifndef WIN32
         usleep(100000);
      #else
         Sleep(1);
      #endif
      return 10000;
   }

   return 0;
}

bool CPeerMgmt::hit(const string& ip, const int& port, const int& session, const int32_t& id)
{
   CGuard recguard(m_PeerRecLock);

   CPeerRecord pr;
   pr.m_strIP = ip;
   pr.m_iPort = port;
   pr.m_iSession = session;
   pr.m_iID = id;

   return m_RecentRec.lookup(&pr) == 0;
}

int CPeerMgmt::setUDTSocket(const std::string& ip, const int& port, const UDTSOCKET& usock)
{
   CGuard recguard(m_PeerRecLock);

   CUDTConns conn;
   conn.m_strIP = ip;
   conn.m_iPort = port;
   conn.m_UDT = usock;
   m_PersistentUDT.update(&conn);

   return 0;
}

int CPeerMgmt::getUDTSocket(const std::string& ip, const int& port, UDTSOCKET& usock)
{
   CGuard recguard(m_PeerRecLock);

   CUDTConns conn;
   conn.m_strIP = ip;
   conn.m_iPort = port;

   if (m_PersistentUDT.lookup(&conn) >= 0)
   {
      usock = conn.m_UDT;
      return 0;
   }

   return -1;
}

void CPeerMgmt::clear()
{
   m_RecentRec.clear();
   m_PeerRTT.clear();
   m_PersistentUDT.clear();
   m_FlowWindow.clear();
}

int32_t CPeerMgmt::hash(const string& ip, const int& port, const int& session, const int32_t& id)
{
   char tmp[1024];
   sprintf(tmp, "%s%d%d%d", ip.c_str(), port, session, id);

   return DHash::hash(tmp, m_uiHashSpace);
}

int32_t CPeerMgmt::hash(const string& ip, const int& port, const int& session)
{
   char tmp[1024];
   sprintf(tmp, "%s%d%d", ip.c_str(), port, session);

   return DHash::hash(tmp, m_uiHashSpace);
}

int32_t CPeerMgmt::hash(const string& ip, const int& port)
{
   char tmp[1024];
   sprintf(tmp, "%s%d", ip.c_str(), port);

   return DHash::hash(tmp, m_uiHashSpace);
}

int32_t CPeerMgmt::hash(const string& ip)
{
   return DHash::hash(ip.c_str(), m_uiHashSpace);
}
