/*****************************************************************************
Copyright � 2006 - 2009, The Board of Trustees of the University of Illinois.
All Rights Reserved.

Sector: A Distributed Storage and Computing Infrastructure

National Center for Data Mining (NCDM)
University of Illinois at Chicago
http://www.ncdm.uic.edu/

Sector is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.

Sector is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu [gu@lac.uic.edu], last updated 01/08/2009
*****************************************************************************/

#include <topology.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include "constant.h"

using namespace std;


int SlaveNode::deserialize(const char* buf, int size)
{
   char* p = (char*)buf;

   m_llTimeStamp = *(int64_t*)p;
   m_llAvailDiskSpace = *(int64_t*)(p + 8);
   m_llTotalFileSize = *(int64_t*)(p + 16);
   m_llCurrMemUsed = *(int64_t*)(p + 24);
   m_llCurrCPUUsed = *(int64_t*)(p + 32);
   m_llTotalInputData = *(int64_t*)(p + 40);
   m_llTotalOutputData = *(int64_t*)(p + 48);

   p += 56;
   int n = *(int32_t*)p;
   p += 4;
   for (int j = 0; j < n; ++ j)
   {
      m_mSysIndInput[p] = *(int64_t*)(p + 16);
      p += 24;
   }
   n = *(int32_t*)p;
   p += 4;
   for (int j = 0; j < n; ++ j)
   {
      m_mSysIndOutput[p] = *(int64_t*)(p + 16);
      p += 24;
   }
   n = *(int32_t*)p;
   p += 4;
   for (int j = 0; j < n; ++ j)
   {
      m_mCliIndInput[p] = *(int64_t*)(p + 16);
      p += 24;
   }
   n = *(int32_t*)p;
   p += 4;
   for (int j = 0; j < n; ++ j)
   {
      m_mCliIndOutput[p] = *(int64_t*)(p + 16);
      p += 24;
   }

   return 0;
}


Topology::Topology():
m_uiLevel(1)
{
}

Topology::~Topology()
{
}

int Topology::init(const char* topoconf)
{
   ifstream ifs(topoconf);

   if (ifs.bad() || ifs.fail())
      return -1;

   char line[128];
   while (!ifs.eof())
   {
      ifs.getline(line, 128);

      if ((strlen(line) == 0) || (line[0] == '#'))
         continue;

      // 192.168.136.0/24	/1/1

      unsigned int p = 0;
      for (; p < strlen(line); ++ p)
      {
         if ((line[p] == ' ') || (line[p] == '\t'))
            break;
      }

      string ip = string(line).substr(0, p);

      for (; p < strlen(line); ++ p)
      {
         if ((line[p] != ' ') && (line[p] != '\t'))
            break;
      }

      string topo = string(line).substr(p + 1, strlen(line));

      TopoMap tm;
      if (parseIPRange(ip.c_str(), tm.m_uiIP, tm.m_uiMask) < 0)
         return -1;
      if (parseTopo(topo.c_str(), tm.m_viPath) <= 0)
         return -1;

      m_vTopoMap.insert(m_vTopoMap.end(), tm);

      if (m_uiLevel < tm.m_viPath.size())
         m_uiLevel = tm.m_viPath.size();
   }

   ifs.close();

   return m_vTopoMap.size();
}

int Topology::lookup(const char* ip, vector<int>& path)
{
   in_addr addr;
   if (inet_pton(AF_INET, ip, &addr) < 0)
      return -1;

   uint32_t digitip = ntohl(addr.s_addr);

   for (vector<TopoMap>::iterator i = m_vTopoMap.begin(); i != m_vTopoMap.end(); ++ i)
   {
      if ((digitip & i->m_uiMask) == (i->m_uiIP & i->m_uiMask))
      {
         path = i->m_viPath;
         return 0;
      }
   }

   for (unsigned int i = 0; i < m_uiLevel; ++ i)
      path.insert(path.end(), 0);

   return -1;
}

unsigned int Topology::match(vector<int>& p1, vector<int>& p2)
{
   unsigned int level;
   if (p1.size() < p2.size())
      level = p1.size();
   else
      level = p2.size();

   for (unsigned int i = 0; i < level; ++ i)
   {
      if (p1[i] != p2[i])
         return i;
   }

   return level;
}

unsigned int Topology::distance(const char* ip1, const char* ip2)
{
   vector<int> p1, p2;
   lookup(ip1, p1);
   lookup(ip2, p2);
   return m_uiLevel - match(p1, p2);
}

unsigned int Topology::distance(const Address& addr, const set<Address, AddrComp>& loclist)
{
   unsigned int dist = 1000000000;
   for (set<Address, AddrComp>::iterator i = loclist.begin(); i != loclist.end(); ++ i)
   {
      unsigned int d = distance(addr.m_strIP.c_str(), i->m_strIP.c_str());
      if (d < dist)
         dist = d;
   }
   return dist;
}

int Topology::getTopoDataSize()
{
   return 8 + m_vTopoMap.size() * (4 + 4 + m_uiLevel * 4);
}

int Topology::serialize(char* buf, int& size)
{
   if (size < int(8 + m_vTopoMap.size() * (4 + 4 + m_uiLevel * 4)))
      return -1;

   size = 8 + m_vTopoMap.size() * (4 + 4 + m_uiLevel * 4);
   int* p = (int*)buf;
   p[0] = m_vTopoMap.size();
   p[1] = m_uiLevel;
   p += 2;

   for (vector<TopoMap>::const_iterator i = m_vTopoMap.begin(); i != m_vTopoMap.end(); ++ i)
   {
      p[0] = i->m_uiIP;
      p[1] = i->m_uiMask;
      p += 2;
      for (vector<int>::const_iterator j = i->m_viPath.begin(); j != i->m_viPath.end(); ++ j)
         *p ++ = *j;
   }

   return 0;
}

int Topology::deserialize(const char* buf, const int& size)
{
   if (size < 8)
      return -1;

   int* p = (int*)buf;
   m_vTopoMap.resize(p[0]);
   m_uiLevel = p[1];
   p += 2;

   if (size < int(8 + m_vTopoMap.size() * (4 + 4 + m_uiLevel * 4)))
      return -1;

   for (vector<TopoMap>::iterator i = m_vTopoMap.begin(); i != m_vTopoMap.end(); ++ i)
   {
      i->m_uiIP = p[0];
      i->m_uiMask = p[1];
      p += 2;
      i->m_viPath.resize(m_uiLevel);
      for (vector<int>::iterator j = i->m_viPath.begin(); j != i->m_viPath.end(); ++ j)
         *j = *p ++;
   }

   return 0;
}

int Topology::parseIPRange(const char* ip, uint32_t& digit, uint32_t& mask)
{
   char buf[128];
   unsigned int i = 0;
   for (unsigned int n = strlen(ip); i < n; ++ i)
   {
      if ('/' == ip[i])
         break;

      buf[i] = ip[i];
   }
   buf[i] = '\0';

   in_addr addr;
   if (inet_pton(AF_INET, buf, &addr) < 0)
      return -1;

   digit = ntohl(addr.s_addr);
   mask = 0xFFFFFFFF;

   if (i == strlen(ip))
      return 0;

   if ('/' != ip[i])
      return -1;
   ++ i;

   int j = 0;
   for (unsigned int n = strlen(ip); i < n; ++ i, ++ j)
      buf[j] = ip[i];
   buf[j] = '\0';

   char* p;
   unsigned int bit = strtol(buf, &p, 10);

   if ((p == buf) || (bit > 32) || (bit < 0))
      return -1;

   mask <<= (32 - bit);

   return 0;
}

int Topology::parseTopo(const char* topo, vector<int>& tm)
{
   char buf[32];
   strncpy(buf, topo, 32);
   int size = strlen(buf);

   for (int i = 0; i < size; ++ i)
   {
      if (buf[i] == '/')
         buf[i] = '\0';
   }

   for (int i = 0; i < size; )
   {
      tm.insert(tm.end(), atoi(buf + i));
      i += strlen(buf + i) + 1;
   }

   return tm.size();
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

   return 1;
}

int SlaveManager::insert(SlaveNode& sn)
{
   sn.m_iNodeID = m_iNodeID ++;
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

   return 1;
}

int SlaveManager::remove(int nodeid)
{
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

   m_mSlaveList.erase(sn);

   return 1;
}

int SlaveManager::chooseReplicaNode(set<int>& loclist, SlaveNode& sn, const int64_t& filesize)
{
   vector< set<int> > avail;
   avail.resize(m_Topology.m_uiLevel + 1);
   for (map<int, SlaveNode>::iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
   {
      // only nodes with more than 10GB disk space are chosen
      if (i->second.m_llAvailDiskSpace < (10000000000LL + filesize))
         continue;

      int level = 0;
      for (set<int>::iterator j = loclist.begin(); j != loclist.end(); ++ j)
      {
         if (i->first == *j)
         {
            level = -1;
            break;
         }

         int tmpl = m_Topology.match(i->second.m_viPath, m_mSlaveList[*j].m_viPath);
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
      return -1;

   timeval t;
   gettimeofday(&t, 0);
   srand(t.tv_usec);

   int r = int(candidate.size() * rand() / (RAND_MAX + 1.0));

   set<int>::iterator n = candidate.begin();
   for (int i = 0; i < r; ++ i)
      n ++;

   sn = m_mSlaveList[*n];

   return 1;
}
 
int SlaveManager::chooseIONode(set<int>& loclist, const Address& client, int mode, map<int, Address>& loc, int replica)
{
   timeval t;
   gettimeofday(&t, 0);
   srand(t.tv_usec);

   if (!loclist.empty())
   {
      // find nearest node, if equal distance, choose a random one
      set<int>::iterator n = loclist.begin();
      unsigned int dist = 1000000000;
      for (set<int>::iterator i = loclist.begin(); i != loclist.end(); ++ i)
      {
         unsigned int d = m_Topology.distance(client.m_strIP.c_str(), m_mSlaveList[*i].m_strIP.c_str());
         if (d < dist)
         {
            dist = d;
            n = i;
         }
         else if (d == dist)
         {
            if ((rand() % 2) == 0)
               n = i;
         }
      }

      Address addr;
      addr.m_strIP = m_mSlaveList[*n].m_strIP;
      addr.m_iPort = m_mSlaveList[*n].m_iPort;
      loc[m_mSlaveList[*n].m_iNodeID] = addr;

      // if this is a READ_ONLY operation, one node is enough
      if ((mode & SF_MODE::WRITE) == 0)
         return 1;

      for (set<int>::iterator i = loclist.begin(); i != loclist.end(); i ++)
      {
         if (i == n)
            continue;

         addr.m_strIP = m_mSlaveList[*i].m_strIP;
         addr.m_iPort = m_mSlaveList[*i].m_iPort;
         loc[m_mSlaveList[*n].m_iNodeID] = addr;
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
         // only nodes with more than 10GB disk space are chosen
         if (i->second.m_llAvailDiskSpace > 10000000000LL)
            avail.insert(i->first);
      }

      int r = int(avail.size() * rand() / (RAND_MAX + 1.0));
      set<int>::iterator n = avail.begin();
      for (int i = 0; i < r; ++ i)
         n ++;
      Address addr;
      addr.m_strIP = m_mSlaveList[*n].m_strIP;
      addr.m_iPort = m_mSlaveList[*n].m_iPort;
      loc[m_mSlaveList[*n].m_iNodeID] = addr;

      // if this is not a high reliable write, one node is enough
      if ((mode & SF_MODE::HiRELIABLE) == 0)
         return 1;

      // otherwise choose more nodes for immediate replica
      for (int i = 0; i < replica - 1; ++ i)
      {
         SlaveNode sn;
         set<int> locid;
         for (map<int, Address>::iterator j = loc.begin(); j != loc.end(); ++ j)
            locid.insert(j->first);
         if (chooseReplicaNode(locid, sn, 10000000000LL) < 0)
            continue;

         addr.m_strIP = sn.m_strIP;
         addr.m_iPort = sn.m_iPort;
         loc[sn.m_iNodeID] = addr;
      }
   }

   return loc.size();
}

int SlaveManager::chooseReplicaNode(set<Address, AddrComp>& loclist, SlaveNode& sn, const int64_t& filesize)
{
   set<int> locid;
   for (set<Address>::iterator i = loclist.begin(); i != loclist.end(); ++ i)
   {
      locid.insert(m_mAddrList[*i]);
   }

   return chooseReplicaNode(locid, sn, filesize);
}

int SlaveManager::chooseIONode(set<Address, AddrComp>& loclist, const Address& client, int mode, map<int, Address>& loc, int replica)
{
   set<int> locid;
   for (set<Address>::iterator i = loclist.begin(); i != loclist.end(); ++ i)
   {
      locid.insert(m_mAddrList[*i]);
   }

   return chooseIONode(locid, client, mode, loc, replica);
}

unsigned int SlaveManager::getTotalSlaves()
{
   return m_mSlaveList.size();
}

uint64_t SlaveManager::getTotalDiskSpace()
{
   uint64_t size = 0;
   for (map<int, SlaveNode>::iterator i = m_mSlaveList.begin(); i != m_mSlaveList.end(); ++ i)
   {
      size += i->second.m_llAvailDiskSpace;
   }

   return size;
}

void SlaveManager::updateClusterStat(Cluster& c)
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
         updateClusterIO(c, m_mSlaveList[*i].m_mSysIndInput, c.m_mSysIndInput, c.m_llTotalInputData);
         updateClusterIO(c, m_mSlaveList[*i].m_mSysIndOutput, c.m_mSysIndOutput, c.m_llTotalOutputData);
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
         updateClusterStat(i->second);

         c.m_llAvailDiskSpace += i->second.m_llAvailDiskSpace;
         c.m_llTotalFileSize += i->second.m_llTotalFileSize;
         updateClusterIO(c, i->second.m_mSysIndInput, c.m_mSysIndInput, c.m_llTotalInputData);
         updateClusterIO(c, i->second.m_mSysIndOutput, c.m_mSysIndOutput, c.m_llTotalOutputData);
      }
   }
}

void SlaveManager::updateClusterIO(Cluster& c, map<string, int64_t>& data_in, map<string, int64_t>& data_out, int64_t& total)
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
