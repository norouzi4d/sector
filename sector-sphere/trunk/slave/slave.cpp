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
   Yunhong Gu, last updated 09/28/2010
*****************************************************************************/

#include "slave.h"
#include <ssltransport.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <sys/times.h>
#include <utime.h>
#include <common.h>
#include <iostream>

using namespace std;

Slave::Slave():
m_iSlaveID(-1),
m_iDataPort(0),
m_iLocalPort(0),
m_iMasterPort(0),
m_strBase("./"),
m_pLocalFile(NULL),
m_bRunning(false),
m_bDiskHealth(true),
m_bNetworkHealth(true)
{
   CGuard::createMutex(m_RunLock);
   CGuard::createCond(m_RunCond);
}

Slave::~Slave()
{
   delete m_pLocalFile;
   CGuard::releaseMutex(m_RunLock);
   CGuard::releaseCond(m_RunCond);
}

int Slave::init(const char* base)
{
   if (NULL != base)
      m_strBase = base;
   else if (ConfLocation::locate(m_strBase) < 0)
   {
      cerr << "unable to locate configuration file; quit.\n";
      return -1;
   }

   string conf = m_strBase + "/conf/slave.conf";
   if (m_SysConfig.init(conf) < 0)
   {
      cerr << "unable to locate or initialize from configuration file; quit.\n";
      return -1;
   }

   // obtain master IP address
   m_strMasterHost = m_SysConfig.m_strMasterHost;
   struct hostent* masterip = gethostbyname(m_strMasterHost.c_str());
   if (NULL == masterip)
   {
      cerr << "invalid master address " << m_strMasterHost << endl;
      return -1;
   }
   char buf[64];
   m_strMasterIP = inet_ntop(AF_INET, masterip->h_addr_list[0], buf, 64);
   m_iMasterPort = m_SysConfig.m_iMasterPort;

   UDTTransport::initialize();

   // init GMP
   m_GMP.init(0);
   m_iLocalPort = m_GMP.getPort();

   // initialize local directory
   m_strHomeDir = m_SysConfig.m_strHomeDir;

   // check local directory
   if (createSysDir() < 0)
   {
      cerr << "unable to create system directory " << m_strHomeDir << endl;
      return -1;
   }

   // initialize slave log
   m_SectorLog.init((m_strHomeDir + "/.log").c_str());

   //copy permanent sphere libraries
   system((string("cp ") + m_strBase + "/slave/sphere/*.so " + m_strHomeDir + "/.sphere/perm/").c_str());

   cout << "scanning " << m_strHomeDir << endl;
   if (m_SysConfig.m_MetaType == DISK)
      m_pLocalFile = new Index2;
   else
      m_pLocalFile = new Index;
   m_pLocalFile->init(m_SysConfig.m_strHomeDir + ".metadata");
   m_pLocalFile->scan(m_strHomeDir.c_str(), "/");

   m_SectorLog.insert("Sector slave started.");

   return 1;
}

int Slave::connect()
{
   // join the server
   SSLTransport::init();

   string cert = m_strBase + "/conf/master_node.cert";

   // calculate total available disk size
   struct statfs64 slavefs;
   statfs64(m_SysConfig.m_strHomeDir.c_str(), &slavefs);
   int64_t availdisk = slavefs.f_bfree * slavefs.f_bsize;

   m_iSlaveID = -1;

   m_pLocalFile->serialize("/", m_strHomeDir + ".tmp/metadata.dat");

   set<Address, AddrComp> masters;
   Address m;
   m.m_strIP = m_strMasterIP;
   m.m_iPort = m_iMasterPort;
   masters.insert(m);
   bool first = true;

   while (!masters.empty())
   {
      string mip = masters.begin()->m_strIP;
      int mport = masters.begin()->m_iPort;
      masters.erase(masters.begin());

      SSLTransport secconn;
      if ((secconn.initClientCTX(cert.c_str()) < 0) ||
         (secconn.open(NULL, 0) < 0) ||
         (secconn.connect(mip.c_str(), mport) < 0))
      {
         cerr << "unable to set up secure channel to the master.\n";
         return -1;
      }

      if (first)
      {
         secconn.getLocalIP(m_strLocalHost);

         // init data exchange channel
         m_iDataPort = 0;
         if (m_DataChn.init(m_strLocalHost, m_iDataPort) < 0)
         {
            cerr << "unable to create data channel.\n";
            secconn.close();
            return -1;
         }
      }

      int32_t cmd = 1;
      secconn.send((char*)&cmd, 4);

      int32_t size = m_strHomeDir.length() + 1;
      secconn.send((char*)&size, 4);
      secconn.send(m_strHomeDir.c_str(), size);

      int32_t res = -1;
      secconn.recv((char*)&res, 4);
      if (res < 0)
      {
         cerr << "slave join rejected. code: " << res << endl;
         return res;
      }

      secconn.send((char*)&m_iLocalPort, 4);
      secconn.send((char*)&m_iDataPort, 4);
      secconn.send((char*)&(availdisk), 8);
      secconn.send((char*)&(m_iSlaveID), 4);

      if (first)
         m_iSlaveID = res;

      struct stat s;
      stat((m_strHomeDir + ".tmp/metadata.dat").c_str(), &s);
      size = s.st_size;
      secconn.send((char*)&size, 4);
      secconn.sendfile((m_strHomeDir + ".tmp/metadata.dat").c_str(), 0, size);

      if (!first)
      {
         secconn.close();
         continue;
      }

      // move out-of-date files to the ".attic" directory
      size = 0;
      secconn.recv((char*)&size, 4);

      if (size > 0)
      {
         secconn.recvfile((m_strHomeDir + ".tmp/metadata.left.dat").c_str(), 0, size);

         Metadata* attic = NULL;
         if (m_SysConfig.m_MetaType == DISK)
            attic = new Index2;
         else
            attic = new Index;
         attic->init(m_strHomeDir + ".tmp/metadata.left");
         attic->deserialize("/", m_strHomeDir + ".tmp/metadata.left.dat", NULL);
         unlink((m_strHomeDir + ".tmp/metadata.left.dat").c_str());

         vector<string> fl;
         attic->list_r("/", fl);
         for (vector<string>::iterator i = fl.begin(); i != fl.end(); ++ i)
         {
            string dst = ".attic/";
            size_t p = i->rfind('/');
            if (p != string::npos)
               dst += i->substr(0, p);
            move(*i, dst, "");
         }

         attic->clear();
         delete attic;

         m_SectorLog.insert("WARNING: certain files have been moved to ./attic due to conflicts.");
      }

      int id = 0;
      secconn.recv((char*)&id, 4);
      Address addr;
      addr.m_strIP = mip;
      addr.m_iPort = mport;
      m_Routing.insert(id, addr);

      int num;
      secconn.recv((char*)&num, 4);
      for (int i = 0; i < num; ++ i)
      {
         char ip[64];
         size = 0;
         secconn.recv((char*)&id, 4);
         secconn.recv((char*)&size, 4);
         secconn.recv(ip, size);
         addr.m_strIP = ip;
         secconn.recv((char*)&addr.m_iPort, 4);

         m_Routing.insert(id, addr);

         masters.insert(addr);
      }

      first = false;
      secconn.close();
   }

   SSLTransport::destroy();

   unlink((m_strHomeDir + ".tmp/metadata.dat").c_str());

   // initialize slave statistics
   m_SlaveStat.init();

   cout << "This Sector slave is successfully initialized and running now.\n";

   return 1;
}

void Slave::run()
{
   string ip;
   int port;
   int32_t id;
   SectorMsg* msg = new SectorMsg;
   msg->resize(65536);

   cout << "slave process: " << "GMP " << m_iLocalPort << " DATA " << m_DataChn.getPort() << endl;

   m_bRunning = true;

#ifndef WIN32
   pthread_t worker_thread;
   pthread_create(&worker_thread, NULL, worker, this);
#else
   unsigned int ThreadID;
   worker_thread = (HANDLE)_beginthreadex(NULL, 0, worker, this, NULL, &ThreadID);
#endif

   while (m_bRunning)
   {
      if (m_GMP.recvfrom(ip, port, id, msg) < 0)
         break;

      m_SectorLog << LogStringTag(LogTag::START, LogLevel::SCREEN) << "recv cmd " << ip << " " << port << " type " << msg->getType() << LogStringTag(LogTag::END);

      // a slave only accepts commands from the masters
      Address addr;
      addr.m_strIP = ip;
      addr.m_iPort = port;
      if (m_Routing.getRouterID(addr) < 0)
         continue;

      switch (msg->getType() / 100)
      {
      case 0:
         processSysCmd(ip, port, id, msg);
         break;

      case 1:
         processFSCmd(ip, port, id, msg);
         break;

      case 2:
         processDCCmd(ip, port, id, msg);
         break;

      case 3:
         processDBCmd(ip, port, id, msg);
         break;

      case 10:
         processMCmd(ip, port, id, msg);
         break;

      #ifdef DEBUG
         processDebugCmd(ip, port, id, msg);
         break;
      #endif

      default:
         break;
      }
   }

   delete msg;

#ifndef WIN32
   pthread_join(worker_thread, NULL);
#else
   WaitForSingleObject(worker_thread, INFINITE);
#endif

   // TODO: check and cancel all file&spe threads

   cout << "slave is stopped by master\n";
}

void Slave::close()
{
   UDTTransport::release();
}

int Slave::processSysCmd(const string& ip, const int port, int id, SectorMsg* msg)
{
   switch (msg->getType())
   {
   case 1: // probe
   {
      m_GMP.sendto(ip, port, id, msg);
      break;
   }

   case 8: // stop
   {
      // stop the slave node

      m_bRunning = false;

      pthread_mutex_lock(&m_RunLock);
      pthread_cond_signal(&m_RunCond);
      pthread_mutex_unlock(&m_RunLock);

      break;
   }

   default:
      return -1;
   }

   return 0;
}

int Slave::processFSCmd(const string& ip, const int port, int id, SectorMsg* msg)
{
   switch (msg->getType())
   {
   case 103: // mkdir
   {
      createDir(msg->getData());
      // TODO: update metatdata

      m_SectorLog << LogStringTag(LogTag::START, LogLevel::LEVEL_3) << "created new directory " << msg->getData() << LogStringTag(LogTag::END);

      break;
   }

   case 104: // move dir/file
   {
      string src = msg->getData();
      string dst = msg->getData() + src.length() + 1;
      string newname = msg->getData() + src.length() + 1 + dst.length() + 1;

      src = Metadata::revisePath(src);
      dst = Metadata::revisePath(dst);
      newname = Metadata::revisePath(newname);

      m_pLocalFile->move(src.c_str(), dst.c_str(), newname.c_str());
      move(src, dst, newname);

      m_SectorLog << LogStringTag(LogTag::START, LogLevel::LEVEL_3) << "dir/file moved from " << src << " to " << dst << "/" << newname << LogStringTag(LogTag::END);

      break;
   }

   case 105: // remove dir/file
   {
      string path = Metadata::revisePath(msg->getData());
      m_pLocalFile->remove(path, true);

      //TODO: removed files may be moved to .attic instead of removing immediately, thus users may be able to restore them

      string sysrm = string("rm -rf ") + reviseSysCmdPath(m_strHomeDir) + reviseSysCmdPath(path);
      system(sysrm.c_str());

      m_SectorLog << LogStringTag(LogTag::START, LogLevel::LEVEL_3) << "dir/file " << path << " is deleted." << LogStringTag(LogTag::END);

      break;
   }

   case 107: // utime
   {
      char* path = msg->getData();

      utimbuf ut;
      ut.actime = *(int64_t*)(msg->getData() + strlen(path) + 1);
      ut.modtime = *(int64_t*)(msg->getData() + strlen(path) + 1);;
      utime((m_strHomeDir + path).c_str(), &ut);

      m_SectorLog << LogStringTag(LogTag::START, LogLevel::LEVEL_3) << "dir/file " << path << " timestamp changed " << LogStringTag(LogTag::END);

      break;
   }

   case 110: // open file
   {
      Param2* p = new Param2;
      p->serv_instance = this;
      p->client_ip = msg->getData();
      p->client_port = *(int*)(msg->getData() + 64);
      p->key = *(int*)(msg->getData() + 136);
      p->mode = *(int*)(msg->getData() + 140);
      p->transid = *(int*)(msg->getData() + 144);
      memcpy(p->crypto_key, msg->getData() + 148, 16);
      memcpy(p->crypto_iv, msg->getData() + 164, 8);
      p->filename = msg->getData() + 172;

      p->master_ip = ip;
      p->master_port = port;

      // the slave must also lock the file IO. Because there are multiple master servers,
      // if one master is down, locks on this file may be lost when another master take over control of the file.
      if (m_pLocalFile->lock(p->filename, p->key, p->mode) < 0)
      {
         msg->setType(-msg->getType());
         msg->m_iDataLength = SectorMsg::m_iHdrSize;
         m_GMP.sendto(ip, port, id, msg);
         delete p;
         break;
      }

      m_SectorLog << LogStringTag(LogTag::START, LogLevel::LEVEL_1) << "opened file " << p->filename << " from " << p->client_ip << ":" << p->client_port << LogStringTag(LogTag::END);

      m_TransManager.addSlave(p->transid, m_iSlaveID);

#ifndef WIN32
      pthread_t file_handler;
      pthread_create(&file_handler, NULL, fileHandler, p);
      pthread_detach(file_handler);
#else
      unsigned int ThreadID;
      HANDLE file_handler = (HANDLE)_beginthreadex(NULL, 0, fileHandler, p, NULL, &ThreadID);
      if (file_handler)
         CloseHandle(file_handler);
#endif

      msg->m_iDataLength = SectorMsg::m_iHdrSize;
      m_GMP.sendto(ip, port, id, msg);

      break;
   }

   case 111: // create a replica to local
   {
      Param3* p = new Param3;
      p->serv_instance = this;
      p->transid = *(int32_t*)msg->getData();
      p->src = msg->getData() + 4;
      p->dst = msg->getData() + 4 + p->src.length() + 1;

      p->master_ip = ip;
      p->master_port = port;

      m_SectorLog << LogStringTag(LogTag::START, LogLevel::LEVEL_3) << "creating replica " << p->src << " " << p->dst << LogStringTag(LogTag::END);

      m_TransManager.addSlave(p->transid, m_iSlaveID);

#ifndef WIN32
      pthread_t replica_handler;
      pthread_create(&replica_handler, NULL, copy, p);
      pthread_detach(replica_handler);
#else
      unsigned int ThreadID;
      HANDLE replica_handler = (HANDLE)_beginthreadex(NULL, 0, copy, p, NULL, &ThreadID);
      if (replica_handler)
         CloseHandle(replica_handler);
#endif

      m_GMP.sendto(ip, port, id, msg);

      break;
   }

   default:
      return -1;
   }

   return 0;
}

int Slave::processDCCmd(const string& ip, const int port, int id, SectorMsg* msg)
{
   switch (msg->getType())
   {
   case 203: // processing engine
   {
      Param4* p = new Param4;
      p->serv_instance = this;
      p->client_ip = msg->getData();
      p->client_ctrl_port = *(int32_t*)(msg->getData() + 64);
      p->client_data_port = *(int32_t*)(msg->getData() + 68);
      p->speid = *(int32_t*)(msg->getData() + 72);
      p->key = *(int32_t*)(msg->getData() + 76);
      p->function = msg->getData() + 80;
      int offset = 80 + p->function.length() + 1;
      p->rows = *(int32_t*)(msg->getData() + offset);
      p->psize = *(int32_t*)(msg->getData() + offset + 4);
      if (p->psize > 0)
      {
         p->param = new char[p->psize];
         memcpy(p->param, msg->getData() + offset + 8, p->psize);
      }
      else
         p->param = NULL;
      p->type = *(int32_t*)(msg->getData() + msg->m_iDataLength - SectorMsg::m_iHdrSize - 8);
      p->transid = *(int32_t*)(msg->getData() + msg->m_iDataLength - SectorMsg::m_iHdrSize - 4);

      p->master_ip = ip;
      p->master_port = port;

      m_SectorLog << LogStringTag(LogTag::START, LogLevel::LEVEL_3) << "starting SPE ... " << p->speid << " " << p->client_data_port << " " << p->function << " " << p->transid << LogStringTag(LogTag::END);

      m_TransManager.addSlave(p->transid, m_iSlaveID);

#ifndef WIN32
      pthread_t spe_handler;
      pthread_create(&spe_handler, NULL, SPEHandler, p);
      pthread_detach(spe_handler);
#else
      unsigned int ThreadID;
      HANDLE spe_handler = (HANDLE)_beginthreadex(NULL, 0, SPEHandler, p, NULL, &ThreadID);
      if (spe_handler)
         CloseHandle(spe_handler);
#endif

      msg->m_iDataLength = SectorMsg::m_iHdrSize;
      m_GMP.sendto(ip, port, id, msg);

      break;
   }

   case 204: // accept SPE buckets
   {
      CGMP* gmp = new CGMP;
      gmp->init();

      Param5* p = new Param5;
      p->serv_instance = this;
      p->client_ip = msg->getData();
      p->client_ctrl_port = *(int32_t*)(msg->getData() + 64);
      p->bucketnum = *(int32_t*)(msg->getData() + 68);
      p->bucketid = *(int32_t*)(msg->getData() + 72);
      p->path = msg->getData() + 80;
      int offset = 80 + p->path.length() + 1 + 4;

      p->filename = msg->getData() + offset;
      p->gmp = gmp;

      offset += p->filename.length() + 1;

      p->key = *(int32_t*)(msg->getData() + offset);
      p->type = *(int32_t*)(msg->getData() + offset + 4);
      if (p->type == 1)
      {
         p->function = msg->getData() + offset + 4 + 4 + 4;
      }
      p->transid = *(int32_t*)(msg->getData() + msg->m_iDataLength - SectorMsg::m_iHdrSize - 8);
      p->client_data_port = *(int32_t*)(msg->getData() + msg->m_iDataLength - SectorMsg::m_iHdrSize - 4);

      p->master_ip = ip;
      p->master_port = port;

      m_SectorLog << LogStringTag(LogTag::START, LogLevel::LEVEL_3) << "starting SPE Bucket ... " << p->filename << " " << p->key << " " << p->type << " " << p->transid << LogStringTag(LogTag::END);

      m_TransManager.addSlave(p->transid, m_iSlaveID);

#ifndef WIN32
      pthread_t spe_shuffler;
      pthread_create(&spe_shuffler, NULL, SPEShuffler, p);
      pthread_detach(spe_shuffler);
#else
      unsigned int ThreadID;
      HANDLE spe_shuffler = (HANDLE)_beginthreadex(NULL, 0, SPEShuffler, p, NULL, &ThreadID);
      if (spe_shuffler)
         CloseHandle(spe_shuffler);
#endif

      *(int32_t*)msg->getData() = gmp->getPort();
      msg->m_iDataLength = SectorMsg::m_iHdrSize + 4;
      m_GMP.sendto(ip, port, id, msg);

      break;
   }

   default:
      return -1;
   }

   return 0;
}

int Slave::processDBCmd(const string& /*ip*/, const int /*port*/, int /*id*/, SectorMsg* msg)
{
   switch (msg->getType())
   {
   case 301: // create a new table

   case 302: // add a new attribute

   case 303: // delete an attribute

   case 304: // delete a table

   default:
      return -1;
   }

   return 0;
}

int Slave::processMCmd(const string& ip, const int port, int id, SectorMsg* msg)
{
   switch (msg->getType())
   {
   case 1001: // new master
   {
      int32_t key = *(int32_t*)msg->getData();
      Address addr;
      addr.m_strIP = msg->getData() + 4;
      addr.m_iPort = *(int32_t*)(msg->getData() + 68);
      m_Routing.insert(key, addr);
      msg->m_iDataLength = SectorMsg::m_iHdrSize + 4;
      m_GMP.sendto(ip, port, id, msg);

      break;
   }

   case 1006: // master lost
   {
      m_Routing.remove(*(int32_t*)msg->getData());
      msg->m_iDataLength = SectorMsg::m_iHdrSize + 4;
      m_GMP.sendto(ip, port, id, msg);
      break;
   }

   default:
      return -1; 
   }

   return 0;
}

#ifdef DEBUG
int Slave::processDebugCmd(const string& /*ip*/, const int /*port*/, int /*id*/, SectorMsg* msg)
{
   switch (msg->getType())
   {
   case 9901: // enable pseudo disk error
      m_bDiskHealth = false;
      break;

   case 9902: // enable pseudo network error
      m_bNetworkHealth = false;
      break;

   default:
      return -1;
   }

   return 0;
}
#endif

int Slave::report(const string& master_ip, const int& master_port, const int32_t& transid, const string& filename, const int32_t& change)
{
   vector<string> filelist;

   if (change > 0)
   {
      struct stat64 s;
      if (stat64((m_strHomeDir + filename).c_str(), &s) >= 0)
      {
         filelist.push_back(filename);

         if (S_ISDIR(s.st_mode))
            getFileList(filename, filelist);
      }
      else
      {
         //log error
      }
   }

   return report(master_ip, master_port, transid, filelist, change);
}

int Slave::getFileList(const string& path, vector<string>& filelist)
{
   vector<SNode> sl;
   if (LocalFS::list_dir(m_strHomeDir + path, sl) < 0)
      return -1;

   for (vector<SNode>::iterator i = sl.begin(); i != sl.end(); ++ i)
   {
      filelist.push_back(path + "/" + i->m_strName);

      if (i->m_bIsDir)
      {
         getFileList(path + "/" + i->m_strName, filelist);
      }
   }

   return filelist.size();
}

int Slave::report(const string& master_ip, const int& master_port, const int32_t& transid, const vector<string>& filelist, const int32_t& change)
{
   vector<string> serlist;
   if (change != FileChangeType::FILE_UPDATE_NO)
   {
      for (vector<string>::const_iterator i = filelist.begin(); i != filelist.end(); ++ i)
      {
         struct stat64 s;
         if (-1 == stat64((m_strHomeDir + *i).c_str(), &s))
            continue;

         SNode sn;
         sn.m_strName = *i;
         sn.m_bIsDir = S_ISDIR(s.st_mode) ? 1 : 0;
         sn.m_llTimeStamp = s.st_mtime;
         sn.m_llSize = s.st_size;

         Address addr;
         addr.m_strIP = "127.0.0.1";
         addr.m_iPort = 0;
         sn.m_sLocation.insert(addr);

         if (change == FileChangeType::FILE_UPDATE_WRITE)
         {
            // file may be created on write; in this case, create a new meta entry instead of update non-existing one
            if (m_pLocalFile->update(sn.m_strName, sn.m_llTimeStamp, sn.m_llSize) < 0)
               m_pLocalFile->create(sn);
         }
         else if (change == FileChangeType::FILE_UPDATE_NEW)
            m_pLocalFile->create(sn);
         else if (change == FileChangeType::FILE_UPDATE_REPLICA)
            m_pLocalFile->create(sn);

         char* buf = NULL;
         sn.serialize(buf);
         serlist.push_back(buf);
         delete [] buf;
      }
   }

   SectorMsg msg;
   msg.setType(1);
   msg.setKey(0);
   msg.setData(0, (char*)&transid, 4);
   msg.setData(4, (char*)&m_iSlaveID, 4);
   msg.setData(8, (char*)&change, 4);
   int32_t num = serlist.size();
   msg.setData(12, (char*)&num, 4);
   int pos = 16;
   for (vector<string>::iterator i = serlist.begin(); i != serlist.end(); ++ i)
   {
      int32_t bufsize = i->length() + 1;
      msg.setData(pos, (char*)&bufsize, 4);
      msg.setData(pos + 4, i->c_str(), bufsize);
      pos += bufsize + 4;
   }

   //TODO: if the current master is down, try a different master
   if (m_GMP.rpc(master_ip.c_str(), master_port, &msg, &msg) < 0)
      return -1;

   if (msg.getType() < 0)
      return *(int32_t*)msg.getData();

   return 1;
}

int Slave::reportMO(const string& master_ip, const int& master_port, const int32_t& transid)
{
   vector<MemObj> tba;
   vector<string> tbd;
   if (m_InMemoryObjects.update(tba, tbd) <= 0)
      return 0;

   if (!tba.empty())
   {
      vector<string> serlist;
      for (vector<MemObj>::const_iterator i = tba.begin(); i != tba.end(); ++ i)
      {
         SNode sn;
         sn.m_strName = i->m_strName;
         sn.m_bIsDir = 0;
         sn.m_llTimeStamp = i->m_llCreationTime;
         sn.m_llSize = 8;

         char* buf = NULL;
         sn.serialize(buf);
         serlist.push_back(buf);
         delete [] buf;
      }

      SectorMsg msg;
      msg.setType(1);
      msg.setKey(0);
      msg.setData(0, (char*)&transid, 4);
      msg.setData(4, (char*)&m_iSlaveID, 4);
      int32_t num = serlist.size();
      msg.setData(8, (char*)&num, 4);
      int pos = 12;
      for (vector<string>::iterator i = serlist.begin(); i != serlist.end(); ++ i)
      {
         int32_t bufsize = i->length() + 1;
         msg.setData(pos, (char*)&bufsize, 4);
         msg.setData(pos + 4, i->c_str(), bufsize);
         pos += bufsize + 4;
      }

      if (m_GMP.rpc(master_ip.c_str(), master_port, &msg, &msg) < 0)
         return -1;
   }

   if (!tbd.empty())
   {
      SectorMsg msg;
      msg.setType(7);
      msg.setKey(0);
      msg.setData(0, (char*)&transid, 4);
      msg.setData(4, (char*)&m_iSlaveID, 4);
      int32_t num = tbd.size();
      msg.setData(8, (char*)&num, 4);
      int pos = 12;
      for (vector<string>::iterator i = tbd.begin(); i != tbd.end(); ++ i)
      {
         int32_t bufsize = i->length() + 1;
         msg.setData(pos, (char*)&bufsize, 4);
         msg.setData(pos + 4, i->c_str(), bufsize);
         pos += bufsize + 4;
      }

      if (m_GMP.rpc(master_ip.c_str(), master_port, &msg, &msg) < 0)
         return -1;
   }

   return 0;
}

int Slave::reportSphere(const string& master_ip, const int& master_port, const int& transid, const vector<Address>* bad)
{
   SectorMsg msg;
   msg.setType(4);
   msg.setKey(0);
   msg.setData(0, (char*)&transid, 4);
   msg.setData(4, (char*)&m_iSlaveID, 4);

   int num = (NULL == bad) ? 0 : bad->size();
   msg.setData(8, (char*)&num, 4);
   for (int i = 0; i < num; ++ i)
   {
      msg.setData(12 + 68 * i, (*bad)[i].m_strIP.c_str(), (*bad)[i].m_strIP.length() + 1);
      msg.setData(12 + 68 * i + 64, (char*)&((*bad)[i].m_iPort), 4);
   }

   if (m_GMP.rpc(master_ip.c_str(), master_port, &msg, &msg) < 0)
      return -1;

   if (msg.getType() < 0)
      return *(int32_t*)msg.getData();

   return 1;
}

int Slave::createDir(const string& path)
{
   vector<string> dir;
   Metadata::parsePath(path.c_str(), dir);

   string currpath = m_strHomeDir;
   for (vector<string>::iterator i = dir.begin(); i != dir.end(); ++ i)
   {
      currpath += *i;
      if ((-1 == ::mkdir(currpath.c_str(), S_IRWXU)) && (errno != EEXIST))
         return -1;
      currpath += "/";
   }

   return 1;
}

int Slave::createSysDir()
{
   // check local directory
   struct stat s;

   if ((stat(m_strHomeDir.c_str(), &s) < 0) || (!S_ISDIR(s.st_mode)))
   {
      if (errno != ENOENT)
         return -1;

      vector<string> dir;
      Metadata::parsePath(m_strHomeDir.c_str(), dir);

      string currpath = "/";
      for (vector<string>::iterator i = dir.begin(); i != dir.end(); ++ i)
      {
         currpath += *i;
         if (LocalFS::mkdir(currpath) < 0)
            return -1;
         currpath += "/";
      }
   }

   if (LocalFS::mkdir(m_strHomeDir + ".metadata") < 0)
      return -1;
   LocalFS::clean_dir(m_strHomeDir + ".metadata");

   if (LocalFS::mkdir(m_strHomeDir + ".log") < 0)
      return -1;

   if (LocalFS::mkdir(m_strHomeDir + ".sphere") < 0)
      return -1;
   //LocalFS::clean_dir(m_strHomeDir + ".sphere");
   if (LocalFS::mkdir(m_strHomeDir + ".sphere/perm") < 0)
      return -1;

   if (LocalFS::mkdir(m_strHomeDir + ".tmp") < 0)
      return -1;
   LocalFS::clean_dir(m_strHomeDir + ".tmp");

   if (LocalFS::mkdir(m_strHomeDir + ".attic") < 0)
      return -1;
   //TODO: check slave.conf option to defice if to clean .attic
   //LocalFS::clean_dir(m_strHomeDir + ".attic");

   return 0;
}

string Slave::reviseSysCmdPath(const string& path)
{
   string rpath;
   for (const char *p = path.c_str(), *q = path.c_str() + path.length(); p != q; ++ p)
   {
      if ((*p == 32) || (*p == 34) || (*p == 38) || (*p == 39))
         rpath.append(1, char(92));
      rpath.append(1, *p);
   }

   return rpath;
}

int Slave::move(const string& src, const string& dst, const string& newname)
{
   createDir(dst);
   system((string("mv ") + reviseSysCmdPath(m_strHomeDir + src) + " " + reviseSysCmdPath(m_strHomeDir + dst + newname)).c_str());
   return 1;
}


void SlaveStat::init()
{
   m_llStartTime = m_llTimeStamp = CTimer::getTime();
   m_llCurrMemUsed = 0;
   m_llCurrCPUUsed = 0;
   m_llTotalInputData = 0;
   m_llTotalOutputData = 0;
   m_mSysIndInput.clear();
   m_mSysIndOutput.clear();
   m_mCliIndInput.clear();
   m_mCliIndOutput.clear();

   pthread_mutex_init(&m_StatLock, 0);
}

void SlaveStat::refresh()
{
   m_llTimeStamp = CTimer::getTime();

#ifndef WIN32
   // THIS CODE IS FOR LINUX ONLY. NOT PORTABLE
   // TODO: this should be improved

   int pid = getpid();

   char memfile[64];
   sprintf(memfile, "/proc/%d/status", pid);
   ifstream ifs;
   ifs.open(memfile, ios::in);
   char buf[1024];
   for (int i = 0; i < 10; ++ i)
      ifs.getline(buf, 1024);
   string tmp;
   ifs >> tmp;
   ifs >> m_llCurrMemUsed;
   m_llCurrMemUsed *= 1024;
   ifs.close();

   clock_t hz = sysconf(_SC_CLK_TCK);
   tms cputime;
   times(&cputime);
   m_llCurrCPUUsed = (cputime.tms_utime + cputime.tms_stime) * 1000000LL / hz;
#else
    m_llCurrMemUsed = GetWSPrivateMemory(::GetProcessId(::GetCurrentProcess()));
    m_llCurrCPUUsed = GetCpuProcUsage (::GetCurrentProcess());
#endif
}

void SlaveStat::updateIO(const string& ip, const int64_t& size, const int& type)
{
   pthread_mutex_lock(&m_StatLock);

   map<string, int64_t>::iterator a;

   if (type == SYS_IN)
   {
      map<string, int64_t>::iterator a = m_mSysIndInput.find(ip);
      if (a == m_mSysIndInput.end())
      {
         m_mSysIndInput[ip] = 0;
         a = m_mSysIndInput.find(ip);
      }

      a->second += size;
      m_llTotalInputData += size;
   }
   else if (type == SYS_OUT)
   {
      map<string, int64_t>::iterator a = m_mSysIndOutput.find(ip);
      if (a == m_mSysIndOutput.end())
      {
         m_mSysIndOutput[ip] = 0;
         a = m_mSysIndOutput.find(ip);
      }

      a->second += size;
      m_llTotalOutputData += size;
   }
   else if (type == CLI_IN)
   {
      map<string, int64_t>::iterator a = m_mCliIndInput.find(ip);
      if (a == m_mCliIndInput.end())
      {
         m_mCliIndInput[ip] = 0;
         a = m_mCliIndInput.find(ip);
      }

      a->second += size;
      m_llTotalInputData += size;
   }
   else if (type == CLI_OUT)
   {
      map<string, int64_t>::iterator a = m_mCliIndOutput.find(ip);
      if (a == m_mCliIndOutput.end())
      {
         m_mCliIndOutput[ip] = 0;
         a = m_mCliIndOutput.find(ip);
      }

      a->second += size;
      m_llTotalOutputData += size;
   }

   pthread_mutex_unlock(&m_StatLock);
}

int SlaveStat::serializeIOStat(char*& buf, int& size)
{
   size = (m_mSysIndInput.size() + m_mSysIndOutput.size() + m_mCliIndInput.size() + m_mCliIndOutput.size()) * 24 + 16;
   buf = new char[size];

   pthread_mutex_lock(&m_StatLock);

   char* p = buf;
   *(int32_t*)p = m_mSysIndInput.size();
   p += 4;
   for (map<string, int64_t>::iterator i = m_mSysIndInput.begin(); i != m_mSysIndInput.end(); ++ i)
   {
      strcpy(p, i->first.c_str());
      *(int64_t*)(p + 16) = i->second;
      p += 24;
   }

   *(int32_t*)p = m_mSysIndOutput.size();
   p += 4;
   for (map<string, int64_t>::iterator i = m_mSysIndOutput.begin(); i != m_mSysIndOutput.end(); ++ i)
   {
      strcpy(p, i->first.c_str());
      *(int64_t*)(p + 16) = i->second;
      p += 24;
   }

   *(int32_t*)p = m_mCliIndInput.size();
   p += 4;
   for (map<string, int64_t>::iterator i = m_mCliIndInput.begin(); i != m_mCliIndInput.end(); ++ i)
   {
      strcpy(p, i->first.c_str());
      *(int64_t*)(p + 16) = i->second;
      p += 24;
   }

   *(int32_t*)p = m_mCliIndOutput.size();
   p += 4;
   for (map<string, int64_t>::iterator i = m_mCliIndOutput.begin(); i != m_mCliIndOutput.end(); ++ i)
   {
      strcpy(p, i->first.c_str());
      *(int64_t*)(p + 16) = i->second;
      p += 24;
   }

   pthread_mutex_unlock(&m_StatLock);

   return (m_mSysIndInput.size() + m_mSysIndOutput.size() + m_mCliIndInput.size() + m_mCliIndOutput.size()) * 24 + 16;
}

void* Slave::worker(void* param)
{
   Slave* self = (Slave*)param;

   int64_t last_report_time = CTimer::getTime();
   int64_t last_gc_time = CTimer::getTime();

   while (self->m_bRunning)
   {
      timeval t;
      gettimeofday(&t, NULL);
      timespec ts;
      ts.tv_sec  = t.tv_sec + 30;
      ts.tv_nsec = t.tv_usec * 1000;

      pthread_mutex_lock(&self->m_RunLock);
      pthread_cond_timedwait(&self->m_RunCond, &self->m_RunLock, &ts);
      pthread_mutex_unlock(&self->m_RunLock);

      // report to master every half minute
      if (CTimer::getTime() - last_report_time < 30000000)
         continue;

      // calculate total available disk size
      struct statvfs64 slavefs;
      statvfs64(self->m_SysConfig.m_strHomeDir.c_str(), &slavefs);
      self->m_SlaveStat.m_llAvailSize = slavefs.f_bavail * slavefs.f_bsize;
      self->m_SlaveStat.m_llDataSize = self->m_pLocalFile->getTotalDataSize("/");

      // users may limit the maximum disk size used by Sector
      if (self->m_SysConfig.m_llMaxDataSize > 0)
      {
         int64_t avail_limit = self->m_SysConfig.m_llMaxDataSize - self->m_SlaveStat.m_llDataSize;
         if (avail_limit < 0)
            avail_limit = 0;
         if (avail_limit < self->m_SlaveStat.m_llAvailSize)
            self->m_SlaveStat.m_llAvailSize = avail_limit;
      }

      self->m_SlaveStat.refresh();

      SectorMsg msg;
      msg.setType(10);
      msg.setKey(0);
      msg.setData(0, (char*)&(self->m_SlaveStat.m_llTimeStamp), 8);
      msg.setData(8, (char*)&(self->m_SlaveStat.m_llAvailSize), 8);
      msg.setData(16, (char*)&(self->m_SlaveStat.m_llDataSize), 8);
      msg.setData(24, (char*)&(self->m_SlaveStat.m_llCurrMemUsed), 8);
      msg.setData(32, (char*)&(self->m_SlaveStat.m_llCurrCPUUsed), 8);
      msg.setData(40, (char*)&(self->m_SlaveStat.m_llTotalInputData), 8);
      msg.setData(48, (char*)&(self->m_SlaveStat.m_llTotalOutputData), 8);

      char* buf = NULL;
      int size = 0;
      self->m_SlaveStat.serializeIOStat(buf, size);
      msg.setData(56, buf, size);
      delete [] buf;

      map<uint32_t, Address> al;
      self->m_Routing.getListOfMasters(al);

      for (map<uint32_t, Address>::iterator i = al.begin(); i != al.end(); ++ i)
      {
         int id = 0;
         self->m_GMP.sendto(i->second.m_strIP, i->second.m_iPort, id, &msg);
      }

      last_report_time = CTimer::getTime();

      // clean broken data channels every hour
      if (CTimer::getTime() - last_gc_time > 3600000000LL)
      {
         self->m_DataChn.garbageCollect();
         last_gc_time = CTimer::getTime();
      }
   }

   return NULL;
}
