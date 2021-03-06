/*****************************************************************************
Copyright � 2006, 2007, The Board of Trustees of the University of Illinois.
All Rights Reserved.

Group Messaging Protocol (GMP)

National Center for Data Mining (NCDM)
University of Illinois at Chicago
http://www.ncdm.uic.edu/

GMP is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.

GMP is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu [gu@lac.uic.edu], last updated 12/08/2007
*****************************************************************************/

#ifndef WIN32
   #include <sys/time.h>
   #include <time.h>
#else
   #include <windows.h>
#endif

#include <common.h>
#include <prec.h>

using namespace std;
using namespace cb;

CPeerManagement::CPeerManagement()
{
   #ifndef WIN32
      pthread_mutex_init(&m_PeerRecLock, NULL);
   #else
      m_PeerRecLock = CreateMutex(NULL, false, NULL);
   #endif
}

CPeerManagement::~CPeerManagement()
{
   #ifndef WIN32
      pthread_mutex_destroy(&m_PeerRecLock);
   #else
      CloseHandle(m_PeerRecLock);
   #endif
}

void CPeerManagement::insert(const string& ip, const int& port, const int& session, const int32_t& id, const int& rtt)
{
   CGuard recguard(m_PeerRecLock);

   CPeerRecord* pr = new CPeerRecord;
   pr->m_strIP = ip;
   pr->m_iPort = port;
   pr->m_iSession = session;

   set<CPeerRecord*, CFPeerRec>::iterator i = m_sPeerRec.find(pr);

   if (i != m_sPeerRec.end())
   {
      if (id > 0)
         (*i)->m_iID = id;

      if (rtt > 0)
      {
         if (-1 == (*i)->m_iRTT )
            (*i)->m_iRTT = rtt;
         else
            (*i)->m_iRTT = ((*i)->m_iRTT * 7 + rtt) >> 3;
      }

      (*i)->m_llTimeStamp = CTimer::getTime();

      delete pr;
   }
   else
   {
      if (id > 0)
         pr->m_iID = id;
      else
         pr->m_iID = -1;

      if (rtt > 0)
         pr->m_iRTT = rtt;
      else
         pr->m_iRTT = -1;

      pr->m_llTimeStamp = CTimer::getTime();

      m_sPeerRec.insert(pr);
      m_sPeerRecByTS.insert(pr);
      m_sPeerRecByIP.insert(pr);

      if (m_sPeerRecByTS.size() > m_uiRecLimit)
      {
         // delete first one
         set<CPeerRecord*, CFPeerRecByTS>::iterator j = m_sPeerRecByTS.begin();

         CPeerRecord* t = *j;
         m_sPeerRec.erase(t);

         pair<multiset<CPeerRecord*, CFPeerRecByIP>::iterator, multiset<CPeerRecord*, CFPeerRecByIP>::iterator> p;
         p = m_sPeerRecByIP.equal_range(t);
         for (multiset<CPeerRecord*, CFPeerRecByIP>::iterator k = p.first; k != p.second; k ++)
         {
            if ((*k)->m_iPort == t->m_iPort)
            {
               m_sPeerRecByIP.erase(k);
               break;
            }
         }

         m_sPeerRecByTS.erase(t);

         delete t;
      }
   }
}

int CPeerManagement::getRTT(const string& ip)
{
   pair<multiset<CPeerRecord*, CFPeerRecByIP>::iterator, multiset<CPeerRecord*, CFPeerRecByIP>::iterator> p;

   CPeerRecord pr;
   pr.m_strIP = ip;
   int rtt = -1;

   CGuard recguard(m_PeerRecLock);

   p = m_sPeerRecByIP.equal_range(&pr);

   for (multiset<CPeerRecord*, CFPeerRecByIP>::iterator i = p.first; i != p.second; i ++)
   {
      if ((*i)->m_iRTT > 0)
      {
         rtt = (*i)->m_iRTT;
         break;
      }
   }

   return rtt;
}

int CPeerManagement::getLastID(const string& ip, const int& port, const int& session)
{
   CPeerRecord pr;
   pr.m_strIP = ip;
   pr.m_iPort = port;
   pr.m_iSession = session;
   int id = -1;

   CGuard recguard(m_PeerRecLock);

   set<CPeerRecord*, CFPeerRec>::iterator i = m_sPeerRec.find(&pr);
   if (i != m_sPeerRec.end())
      id = (*i)->m_iID;

   return id;
}

void CPeerManagement::clearRTT(const string& ip)
{
   CPeerRecord pr;
   pr.m_strIP = ip;

   CGuard recguard(m_PeerRecLock);

   pair<multiset<CPeerRecord*, CFPeerRecByIP>::iterator, multiset<CPeerRecord*, CFPeerRecByIP>::iterator> p;
   p = m_sPeerRecByIP.equal_range(&pr);
   for (multiset<CPeerRecord*, CFPeerRecByIP>::iterator i = p.first; i != p.second; i ++)
      (*i)->m_iRTT = -1;
}
