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
   Yunhong Gu, last updated 04/25/2010
*****************************************************************************/

#ifndef WIN32
    #include <unistd.h>
    #include <sys/time.h>
#endif
#include <sys/types.h>
#include <cstring>

#include "slavemgmt.h"
#include "meta.h"

using namespace std;

SlaveManager::SlaveManager():
m_llLastUpdateTime(-1),
m_llSlaveMinDiskSpace(10000000000LL)
{
}

SlaveManager::~SlaveManager()
{
}

int SlaveManager::init(const char* topoconf)
{
   m_Cluster.m_iClusterID = 0;
   m_Cluster.m_iTotalNodes = 0;
   m_Cluster.m_llAvailDiskSpace = 0;
   m_Cluster.m_llTotalFileSize = 0;
   m_Cluster.m_llTotalInputData = 0;
   m_Cluster.m_llTotalOutputData = 0;
   m_Cluster.m_viPath.clear();

   if (m_Topology.init(topoconf) < 0)
      return -1;

   Cluster* pc = &m_Cluster;

   // insert 0/0/0/....
   for (unsigned int i = 0; i < m_Topology.m_uiLevel; ++ i)
   {
      Cluster c;
      c.m_iClusterID = 0;
      c.m_iTotalNodes = 0;
      c.m_llAvailDiskSpace = 0;
      c.m_llTotalFileSize = 0;
      c.m_llTotalInputData = 0;
      c.m_llTotalOutputData = 0;
      c.m_viPath = pc->m_viPath;
      c.m_viPath.insert(c.m_viPath.end(), 0);

      pc->m_mSubCluster[0] = c;
      pc = &(pc->m_mSubCluster[0]);
   }

   for (vector<Topology::TopoMap>::iterator i = m_Topology.m_vTopoMap.begin(); i != m_Topology.m_vTopoMap.end(); ++ i)
   {
      pc = &m_Cluster;

      for (vector<int>::iterator l = i->m_viPath.begin(); l != i->m_viPath.end(); ++ l)
      {
         if (pc->m_mSubCluster.find(*l) == pc->m_mSubCluster.end())
         {
            Cluster c;
            c.m_iClusterID = *l;
            c.m_iTotalNodes = 0;
            c.m_llAvailDiskSpace = 0;
            c.m_llTotalFileSize = 0;
            c.m_llTotalInputData = 0;
            c.m_llTotalOutputData = 0;
            c.m_viPath = pc->m_viPath;
            c.m_viPath.insert(c.m_viPath.end(), *l);
            pc->m_mSubCluster[*l] = c;
         }
         pc = &(pc->m_mSubCluster[*l]);
      }
   }

   m_llLastUpdateTime = CTimer::getTime();

   return 1;
}

int SlaveManager::setSlaveMinDiskSpace(const int64_t& byteSize)
{
   m_llSlaveMinDiskSpace = byteSize;
   return 0;
}

int SlaveManager::insert(SlaveNode& sn)
{
   CMutexGuard guard(m_SlaveLock);

   sn.m_llLastUpdateTime = CTimer::getTime();
   sn.m_iRetryNum = 0;
   sn.m_llLastVoteTime = CTimer::getTime();
   sn.m_iStatus = 1;

   m_mSlaveList[sn.m_iNodeID] = sn;

   Address addr;
   addr.m_strIP = sn.m_strIP;
   addr.m_iPort = sn.m_iPort;
   m_mAddrList[addr] = sn.m_iNodeID;

   m_Topology.lookup(sn.m_strIP.c_str(), sn.m_viPath);
   map<int, Cluster>* sc = &(m_Cluster.m_mSubCluster);
   map<int, Cluster>::iterator pc;
   for (vector<int>::iterator i = sn.m_viPath.begin(); i != sn.m_viPath.end(); ++ i)
   {
      pc = sc->find(*i);
      pc->second.m_iTotalNodes ++;
      pc->second.m_llAvailDiskSpace += sn.m_llAvailDiskSpace;
      pc->second.m_llTotalFileSize += sn.m_llTotalFileSize;

      sc = &(pc->second.m_mSubCluster);
   }

   pc->second.m_sNodes.insert(sn.m_iNodeID);

   map<string, set<string> >::iterator i = m_mIPFSInfo.find(sn.m_strIP);
   if (i == m_mIPFSInfo.end())
      m_mIPFSInfo[sn.m_strIP].insert(Metadata::revisePath(sn.m_strStoragePath));
   else
      i->second.insert(sn.m_strStoragePath);

   m_llLastUpdateTime = CTimer::getTime();

   return 1;
}

int SlaveManager::remove(int nodeid)
{
   CMutexGuard guard(m_SlaveLock);

   map<int, SlaveNode>::iterator sn = m_mSlaveList.find(nodeid);

   if (sn == m_mSlaveList.end())
      return -1;

   Address addr;
   addr.m_strIP = sn->second.m_strIP;
   addr.m_iPort = sn->second.m_iPort;
   m_mAddrList.erase(addr);

   vector<int> path;
   m_Topology.lookup(sn->second.m_strIP.c_str(), path);
   map<int, Cluster>* sc = &(m_Cluster.m_mSubCluster);
   map<int, Cluster>::iterator pc;
   for (vector<int>::iterator i = path.begin(); i != path.end(); ++ i)
   {
      if ((pc = sc->find(*i)) == sc->end())
      {
         // something wrong
         break;
      }

      pc->second.m_iTotalNodes --;
      pc->second.m_llAvailDiskSpace -= sn->second.m_llAvailDiskSpace;
      pc->second.m_llTotalFileSize -= sn->second.m_llTotalFileSize;

      sc = &(pc->second.m_mSubCluster);
   }

   pc->second.m_sNodes.erase(nodeid);

   map<string, set<string> >::iterator i = m_mIPFSInfo.find(sn->second.m_strIP);
   if (i != m_mIPFSInfo.end())
   {
      i->second.erase(sn->second.m_strStoragePath);
      if (i->second.empty())
         m_mIPFSInfo.erase(i);
   }
   else
   {
      //something wrong
   }

   m_mSlaveList.erase(sn);
   m_siBadSlaves.erase(nodeid);

   m_llLastUpdateTime = CTimer::getTime();

   return 1;
}

bool SlaveManager::checkDuplicateSlave(const string& ip, const string& path)
{
   CMutexGuard guard(m_SlaveLock);

   map<string, set<string> >::iterator i = m_mIPFSInfo.find(ip);
   if (i == m_mIPFSInfo.end())
      return false;

   string revised_path = Metadata::revisePath(path);
   for (set<string>::iterator j = i->second.begin(); j != i->second.end(); ++ j)
   {
      // if there is overlap between the two storage paths, it means that there is a conflict
      // the new slave should be rejected in this case

      if ((j->find(revised_path) != string::npos) || (revised_path.find(*j) != string::npos))
         return true;
   }

   return false;
}

int SlaveManager::chooseReplicaNode(set<int>& loclist, SlaveNode& sn, const int64_t& filesize)
{
   CMutexGuard guard(m_SlaveLock);

   vector< set<int> > avail;
   avail.resize(m_Topology.m_uiLevel + 1);
   for (map<int, SlaveNode>::iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
   {
      // skip bad slaves
      if (m_siBadSlaves.find(i->first) != m_siBadSlaves.end())
         continue;

      // only nodes with more than minimum availale disk space are chosen
      if (i->second.m_llAvailDiskSpace < (m_llSlaveMinDiskSpace + filesize))
         continue;

      int level = 0;
      for (set<int>::iterator j = loclist.begin(); j != loclist.end(); ++ j)
      {
         if (i->first == *j)
         {
            level = -1;
            break;
         }

         map<int, SlaveNode>::iterator jp = m_mSlaveList.find(*j);
         if (jp == m_mSlaveList.end())
            continue;

         int tmpl = m_Topology.match(i->second.m_viPath, jp->second.m_viPath);
         if (tmpl > level)
            level = tmpl;
      }

      if (level >= 0)
         avail[level].insert(i->first);
   }

   set<int> candidate;
   for (unsigned int i = 0; i <= m_Topology.m_uiLevel; ++ i)
   {
      if (avail[i].size() > 0)
      {
         candidate = avail[i];
         break;
      }
   }

   if (candidate.empty())
      return SectorError::E_NODISK;

   timeval t;
   gettimeofday(&t, 0);
   srand(t.tv_usec);

   int r = int(candidate.size() * (double(rand()) / RAND_MAX));

   set<int>::iterator n = candidate.begin();
   for (int i = 0; i < r; ++ i)
      n ++;

   sn = m_mSlaveList[*n];

   return 1;
}
 
int SlaveManager::chooseIONode(set<int>& loclist, const Address& client, int mode, vector<SlaveNode>& sl, int replica)
{
   CMutexGuard guard(m_SlaveLock);

   timeval t;
   gettimeofday(&t, 0);
   srand(t.tv_usec);

   if (!loclist.empty())
   {
      // find nearest node, if equal distance, choose a random one
      unsigned int dist = 1000000000;
      map<unsigned int, vector<int> > dist_vec;
      for (set<int>::iterator i = loclist.begin(); i != loclist.end(); ++ i)
      {
         unsigned int d = 1000000000;

         // do not choose bad node
         if (m_siBadSlaves.find(*i) == m_siBadSlaves.end())
            d = m_Topology.distance(client.m_strIP.c_str(), m_mSlaveList[*i].m_strIP.c_str());

         dist_vec[d].push_back(*i);

         if (d < dist)
            dist = d;
      }

      int r = int(dist_vec[dist].size() * (double(rand()) / RAND_MAX)) % dist_vec[dist].size();
      vector<int>::iterator n = dist_vec[dist].begin();
      for (int i = 0; i < r; ++ i)
         n ++;
      sl.push_back(m_mSlaveList[*n]);

      // if this is a READ_ONLY operation, one node is enough
      if ((mode & SF_MODE::WRITE) == 0)
         return 1;

      // the first node will be the closest to the client; the client writes to that node only
      for (set<int>::iterator i = loclist.begin(); i != loclist.end(); i ++)
      {
         if (*i == *n)
            continue;

         sl.push_back(m_mSlaveList[*i]);
      }
   }
   else
   {
      // no available nodes for READ_ONLY operation
      if ((mode & SF_MODE::WRITE) == 0)
         return 0;

      set<int> avail;

      for (map<int, SlaveNode>::iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
      {
         // skip bad slaves
         if (m_siBadSlaves.find(i->first) != m_siBadSlaves.end())
            continue;

         // only nodes with more than minimum available disk space are chosen
         if (i->second.m_llAvailDiskSpace > m_llSlaveMinDiskSpace)
            avail.insert(i->first);
      }

      int r = 0;
      if (avail.size() > 0)
         r = int(avail.size() * (double(rand()) / RAND_MAX)) % avail.size();
      set<int>::iterator n = avail.begin();
      for (int i = 0; i < r; ++ i)
         n ++;
      if (!avail.empty())
         sl.push_back(m_mSlaveList[*n]);

      // if this is not a high reliable write, one node is enough
      if ((mode & SF_MODE::HiRELIABLE) == 0)
         return sl.size();

      // otherwise choose more nodes for immediate replica
      for (int i = 0; i < replica - 1; ++ i)
      {
         SlaveNode sn;
         set<int> locid;
         for (vector<SlaveNode>::iterator j = sl.begin(); j != sl.end(); ++ j)
            locid.insert(j->m_iNodeID);
#ifndef WIN32
         pthread_mutex_unlock(&m_SlaveLock.m_Mutex);
#else
         // <slr> OK to lock win32 critical section more than once
#endif
         if (chooseReplicaNode(locid, sn, m_llSlaveMinDiskSpace) < 0)
            continue;

         sl.push_back(sn);
      }
   }

   return sl.size();
}

int SlaveManager::chooseReplicaNode(set<Address, AddrComp>& loclist, SlaveNode& sn, const int64_t& filesize)
{
   set<int> locid;
   for (set<Address, AddrComp>::const_iterator i = loclist.begin(); i != loclist.end(); ++ i)
   {
      locid.insert(m_mAddrList[*i]);
   }

   return chooseReplicaNode(locid, sn, filesize);
}

int SlaveManager::chooseIONode(set<Address, AddrComp>& loclist, const Address& client, int mode, vector<SlaveNode>& sl, int replica)
{
   set<int> locid;
   for (set<Address, AddrComp>::const_iterator i = loclist.begin(); i != loclist.end(); ++ i)
   {
      locid.insert(m_mAddrList[*i]);
   }

   return chooseIONode(locid, client, mode, sl, replica);
}

int SlaveManager::chooseSPENodes(const Address& client, vector<SlaveNode>& sl)
{
   for (map<int, SlaveNode>::const_iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
   {
      // skip bad slaves
      if (m_siBadSlaves.find(i->first) != m_siBadSlaves.end())
         continue;

      // only nodes with more than minimum available disk space are chosen
      if (i->second.m_llAvailDiskSpace < m_llSlaveMinDiskSpace)
         continue;

      sl.push_back(i->second);

      //TODO:: add more creteria to choose nodes
   }

   return sl.size();
}

int SlaveManager::serializeTopo(char*& buf, int& size)
{
   CMutexGuard guard(m_SlaveLock);

   buf = NULL;
   size = m_Topology.getTopoDataSize();
   buf = new char[size];
   m_Topology.serialize(buf, size);

   return size;
}

int SlaveManager::updateSlaveList(vector<Address>& sl, int64_t& last_update_time)
{
   CMutexGuard guard(m_SlaveLock);

   if (last_update_time < m_llLastUpdateTime)
   {
      sl.clear();
      for (map<int, SlaveNode>::iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
      {
         Address addr;
         addr.m_strIP = i->second.m_strIP;
         addr.m_iPort = i->second.m_iPort;
         sl.push_back(addr);
      }
   }

   last_update_time = CTimer::getTime();

   return sl.size();
}

int SlaveManager::updateSlaveInfo(const Address& addr, const char* info, const int& len)
{
   CMutexGuard guard(m_SlaveLock);

   map<Address, int, AddrComp>::iterator a = m_mAddrList.find(addr);
   if (a == m_mAddrList.end())
      return -1;

   map<int, SlaveNode>::iterator s = m_mSlaveList.find(a->second);
   if (s == m_mSlaveList.end())
   {
      //THIS SHOULD NOT HAPPEN
      return -1;
   }

   s->second.m_llLastUpdateTime = CTimer::getTime();
   s->second.deserialize(info, len);
   s->second.m_iRetryNum = 0;

   if (s->second.m_llAvailDiskSpace < m_llSlaveMinDiskSpace)
   {
      if (s->second.m_iStatus == 1)
      {
         //char text[64];
         //sprintf(text, "Slave %s has less than minimum available disk space left.", s->second.m_strIP.c_str());
         //m_SectorLog.insert(text);

         s->second.m_iStatus = 2;
      }
   }
   else
   {
      if (s->second.m_iStatus == 2)
         s->second.m_iStatus = 1;
   }

   return 0;
}

int SlaveManager::increaseRetryCount(const Address& addr)
{
   CMutexGuard guard(m_SlaveLock);

   map<Address, int, AddrComp>::iterator a = m_mAddrList.find(addr);
   if (a == m_mAddrList.end())
      return -1;

   map<int, SlaveNode>::iterator s = m_mSlaveList.find(a->second);
   if (s == m_mSlaveList.end())
   {
      //THIS SHOULD NOT HAPPEN
      return -1;
   }

   s->second.m_iRetryNum ++;
   return 0;
}

int SlaveManager::checkBadAndLost(map<int, Address>& bad, map<int, Address>& lost)
{
   CMutexGuard guard(m_SlaveLock);

   bad.clear();
   lost.clear();

   for (map<int, SlaveNode>::iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
   {
      // clear expired votes
      if (i->second.m_llLastVoteTime - CTimer::getTime() > 24LL * 60 * 3600 * 1000000)
      {
         i->second.m_sBadVote.clear();
         i->second.m_llLastVoteTime = CTimer::getTime();
      }

      // if received more than half votes, it is bad
      if (i->second.m_sBadVote.size() * 2 > m_mSlaveList.size())
      {
         bad[i->first].m_strIP = i->second.m_strIP;
         bad[i->first].m_iPort = i->second.m_iPort;
      }

      if (i->second.m_iRetryNum > 10)
      {
         lost[i->first].m_strIP = i->second.m_strIP;
         lost[i->first].m_iPort = i->second.m_iPort;
      }
   }

   return 0;
}

int SlaveManager::serializeSlaveList(char*& buf, int& size)
{
   CMutexGuard guard(m_SlaveLock);

   buf = new char [(4 + 4 + 64 + 4 + 4) * m_mSlaveList.size()];

   char* p = buf;
   for (map<int, SlaveNode>::iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
   {
      *(int32_t*)p = i->first;
      p += 4;
      *(int32_t*)p = i->second.m_strIP.length() + 1;
      p += 4;
      strcpy(p, i->second.m_strIP.c_str());
      p += i->second.m_strIP.length() + 1;
      *(int32_t*)p = i->second.m_iPort;
      p += 4;
      *(int32_t*)p = i->second.m_iDataPort;
      p += 4;
   }

   size = p - buf;

   return m_mSlaveList.size();
}

int SlaveManager::deserializeSlaveList(int num, const char* buf, int size)
{
   const char* p = buf;
   for (int i = 0; i < num; ++ i)
   {
      SlaveNode sn;
      sn.m_iNodeID = *(int32_t*)p;
      p += 4;
      int32_t size = *(int32_t*)p;
      p += 4;
      sn.m_strIP = p;
      p += size;
      sn.m_iPort = *(int32_t*)p;
      p += 4;
      sn.m_iDataPort = *(int32_t*)p;
      p += 4;

      insert(sn);
   }

   updateClusterStat();

   return 0;
}

int SlaveManager::getSlaveID(const Address& addr)
{
   CMutexGuard guard(m_SlaveLock);

   map<Address, int, AddrComp>::const_iterator i = m_mAddrList.find(addr);

   if (i == m_mAddrList.end())
      return -1;

   return i->second;
}

int SlaveManager::getSlaveAddr(const int& id, Address& addr)
{
   CMutexGuard guard(m_SlaveLock);

   map<int, SlaveNode>::iterator i = m_mSlaveList.find(id);

   if (i == m_mSlaveList.end())
      return -1;

   addr.m_strIP = i->second.m_strIP;
   addr.m_iPort = i->second.m_iPort;

   return 0;
}

int SlaveManager::voteBadSlaves(const Address& voter, int num, const char* buf)
{
   CMutexGuard guard(m_SlaveLock);

   int vid = m_mAddrList[voter];
   for (int i = 0; i < num; ++ i)
   {
      Address addr;
      addr.m_strIP = buf + i * 68;
      addr.m_iPort = *(int*)(buf + i * 68 + 64);

      int slave = m_mAddrList[addr];
      m_mSlaveList[slave].m_sBadVote.insert(vid);
   }

   return 0;
}

unsigned int SlaveManager::getNumberOfClusters()
{
   CMutexGuard guard(m_SlaveLock);

   return m_Cluster.m_mSubCluster.size();
}

unsigned int SlaveManager::getNumberOfSlaves()
{
   CMutexGuard guard(m_SlaveLock);

   return m_mSlaveList.size();
}

int SlaveManager::serializeClusterInfo(char* buf, int& size)
{
   CMutexGuard guard(m_SlaveLock);

   char* p = buf;
   for (map<int, Cluster>::iterator i = m_Cluster.m_mSubCluster.begin(); i != m_Cluster.m_mSubCluster.end(); ++ i)
   {
      *(int64_t*)p = i->second.m_iClusterID;
      *(int64_t*)(p + 8) = i->second.m_iTotalNodes;
      *(int64_t*)(p + 16) = i->second.m_llAvailDiskSpace;
      *(int64_t*)(p + 24) = i->second.m_llTotalFileSize;
      *(int64_t*)(p + 32) = i->second.m_llTotalInputData;
      *(int64_t*)(p + 40) = i->second.m_llTotalOutputData;

      p += 48;
   }

   size = p - buf;
   return size;
}

int SlaveManager::serializeSlaveInfo(char* buf, int& size)
{
   CMutexGuard guard(m_SlaveLock);

   char* p = buf;
   for (map<int, SlaveNode>::iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
   {
      strcpy(p, i->second.m_strIP.c_str());
      *(int64_t*)(p + 16) = i->second.m_llAvailDiskSpace;
      *(int64_t*)(p + 24) = i->second.m_llTotalFileSize;
      *(int64_t*)(p + 32) = i->second.m_llCurrMemUsed;
      *(int64_t*)(p + 40) = i->second.m_llCurrCPUUsed;
      *(int64_t*)(p + 48) = i->second.m_llTotalInputData;
      *(int64_t*)(p + 56) = i->second.m_llTotalOutputData;
      *(int64_t*)(p + 64) = i->second.m_llTimeStamp;

      p += 72;
   }

   size = p - buf;
   return size;
}

uint64_t SlaveManager::getTotalDiskSpace()
{
   CMutexGuard guard(m_SlaveLock);

   uint64_t size = 0;
   for (map<int, SlaveNode>::iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
      size += i->second.m_llAvailDiskSpace;

   return size;
}

void SlaveManager::updateClusterStat()
{
   CMutexGuard guard(m_SlaveLock);

   updateclusterstat_(m_Cluster);
}

void SlaveManager::updateclusterstat_(Cluster& c)
{
   if (c.m_mSubCluster.empty())
   {
      c.m_llAvailDiskSpace = 0;
      c.m_llTotalFileSize = 0;
      c.m_llTotalInputData = 0;
      c.m_llTotalOutputData = 0;
      c.m_mSysIndInput.clear();
      c.m_mSysIndOutput.clear();

      for (set<int>::iterator i = c.m_sNodes.begin(); i != c.m_sNodes.end(); ++ i)
      {
         c.m_llAvailDiskSpace += m_mSlaveList[*i].m_llAvailDiskSpace;
         c.m_llTotalFileSize += m_mSlaveList[*i].m_llTotalFileSize;
         updateclusterio_(c, m_mSlaveList[*i].m_mSysIndInput, c.m_mSysIndInput, c.m_llTotalInputData);
         updateclusterio_(c, m_mSlaveList[*i].m_mSysIndOutput, c.m_mSysIndOutput, c.m_llTotalOutputData);
      }
   }
   else
   {
      c.m_llAvailDiskSpace = 0;
      c.m_llTotalFileSize = 0;
      c.m_llTotalInputData = 0;
      c.m_llTotalOutputData = 0;
      c.m_mSysIndInput.clear();
      c.m_mSysIndOutput.clear();
	    
      for (map<int, Cluster>::iterator i = c.m_mSubCluster.begin(); i != c.m_mSubCluster.end(); ++ i)
      {
         updateclusterstat_(i->second);

         c.m_llAvailDiskSpace += i->second.m_llAvailDiskSpace;
         c.m_llTotalFileSize += i->second.m_llTotalFileSize;
         updateclusterio_(c, i->second.m_mSysIndInput, c.m_mSysIndInput, c.m_llTotalInputData);
         updateclusterio_(c, i->second.m_mSysIndOutput, c.m_mSysIndOutput, c.m_llTotalOutputData);
      }
   }
}

void SlaveManager::updateclusterio_(Cluster& c, map<string, int64_t>& data_in, map<string, int64_t>& data_out, int64_t& total)
{
   for (map<string, int64_t>::iterator p = data_in.begin(); p != data_in.end(); ++ p)
   {
      vector<int> path;
      m_Topology.lookup(p->first.c_str(), path);
      if (m_Topology.match(c.m_viPath, path) == c.m_viPath.size())
         continue;

      map<string, int64_t>::iterator n = data_out.find(p->first);
      if (n == data_out.end())
         data_out[p->first] = p->second;
      else
         n->second += p->second;

      total += p->second;
   }
}
