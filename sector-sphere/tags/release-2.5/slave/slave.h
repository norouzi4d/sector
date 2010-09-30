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


#ifndef __SECTOR_SLAVE_H__
#define __SECTOR_SLAVE_H__

#include <gmp.h>
#include <datachn.h>
#include <conf.h>
#include <index.h>
#include <index2.h>
#include <log.h>
#include <sphere.h>
#include <routing.h>
#include <transaction.h>


typedef int (*SPHERE_PROCESS)(const SInput*, SOutput*, SFile*);
typedef int (*MR_MAP)(const SInput*, SOutput*, SFile*);
typedef int (*MR_PARTITION)(const char*, int, void*, int);
typedef int (*MR_COMPARE)(const char*, int, const char*, int);
typedef int (*MR_REDUCE)(const SInput*, SOutput*, SFile*);


class SPEResult
{
public:
   ~SPEResult();

public:
   void init(const int& n);
   void addData(const int& bucketid, const char* data, const int64_t& len);
   void clear();

public:
   int m_iBucketNum;				// number of buckets

   std::vector<int32_t> m_vIndexLen;		// length of index data for each bucket
   std::vector<int64_t*> m_vIndex;		// index for each bucket
   std::vector<int32_t> m_vIndexPhyLen;		// physical length for each bucket index buffer
   std::vector<int32_t> m_vDataLen;		// data size of each bucket
   std::vector<char*> m_vData;			// bucket data
   std::vector<int32_t> m_vDataPhyLen;		// physical buffer length for each bucket data

   int64_t m_llTotalDataSize;			// total data size for all buckets
};

class SPEDestination
{
public:
   SPEDestination();
   ~SPEDestination();

public:
   void init(const int& buckets);
   void reset(const int& buckets);

public:
   int* m_piSArray;			// data size per bucket, for all buckets
   int* m_piRArray;			// number of rows per bucket, for all buckets

   int m_iLocNum;			// number of locations / shufflers
   char* m_pcOutputLoc;			// shuffler locations [ip, port]
   int* m_piLocID;			// location ID of each bucket

   std::string m_strLocalFile;		// local destination file name prefix, if result is to be written to local disk
   char m_pcLocalFileID[64];		// local file id: file name = prefix + . + id
};

struct MRRecord
{
   char* m_pcData;
   int m_iSize;

   MR_COMPARE m_pCompRoutine;
};

struct ltrec
{
   bool operator()(const MRRecord& r1, const MRRecord& r2) const
   {
      return (r1.m_pCompRoutine(r1.m_pcData, r1.m_iSize, r2.m_pcData, r2.m_iSize) < 0);
   }
};

class SlaveStat
{
public:
   int64_t m_llStartTime;
   int64_t m_llTimeStamp;

   int64_t m_llDataSize;
   int64_t m_llAvailSize;
   int64_t m_llCurrMemUsed;
   int64_t m_llCurrCPUUsed;

   int64_t m_llTotalInputData;
   int64_t m_llTotalOutputData;

   std::map<std::string, int64_t> m_mSysIndInput;
   std::map<std::string, int64_t> m_mSysIndOutput;
   std::map<std::string, int64_t> m_mCliIndInput;
   std::map<std::string, int64_t> m_mCliIndOutput;

public:
   void init();
   void refresh();
   void updateIO(const std::string& ip, const int64_t& size, const int& type);
   int serializeIOStat(char*& buf, int& size);

private:
   pthread_mutex_t m_StatLock;

public: // io statistics type
   static const int SYS_IN = 1;
   static const int SYS_OUT = 2;
   static const int CLI_IN = 3;
   static const int CLI_OUT = 4;
};

class Slave
{
public:
   Slave();
   ~Slave();

public:
   int init(const char* base = NULL);
   int connect();
   void run();
   void close();

private:
   int processSysCmd(const std::string& ip, const int port, int id, SectorMsg* msg);
   int processFSCmd(const std::string& ip, const int port, int id, SectorMsg* msg);
   int processDCCmd(const std::string& ip, const int port, int id, SectorMsg* msg);
   int processDBCmd(const std::string& ip, const int port, int id, SectorMsg* msg);
   int processMCmd(const std::string& ip, const int port, int id, SectorMsg* msg);

   #ifdef DEBUG
   int processDebugCmd(const std::string& ip, const int port, int id, SectorMsg* msg);
   #endif

private:
   struct Param2
   {
      Slave* serv_instance;	// self

      std::string filename;	// filename
      int mode;			// file access mode

      std::string master_ip;
      int master_port;
      int transid;		// transaction ID

      int key;                  // client key
      std::string client_ip;	// downlink IP
      int client_port;		// downlink port

      unsigned char crypto_key[16];
      unsigned char crypto_iv[8];
   };

   struct Param3
   {
      Slave* serv_instance;	// self
      std::string master_ip;
      int master_port;
      int transid;		// transaction id
      std::string src;		// source file
      std::string dst;		// destination file
   };

   struct Param4
   {
      Slave* serv_instance;	// self

      std::string client_ip;	// client IP
      int client_ctrl_port;	// client GMP port
      int client_data_port;	// client data port
      int key;                  // client key

      std::string master_ip;
      int master_port;
      int transid;		// transaction id

      int speid;		// speid

      std::string function;	// SPE or Map operator
      int rows;                 // number of rows per processing: -1 means all in the block
      char* param;		// SPE parameter
      int psize;		// parameter size
      int type;			// process type
   };

   struct Bucket
   {
      int totalnum;		// total number of records
      int totalsize;		// total data size
      std::string src_ip;	// source IP address
      int src_dataport;		// source data port
      int session;		// DataChn session ID
   };

   struct Param5
   {
      Slave* serv_instance;     // self

      std::string master_ip;
      int master_port;
      int transid;		// transaction id

      std::string client_ip;    // client IP
      int client_ctrl_port;     // client GMP port
      int client_data_port;	// client data port
      std::string path;		// output path
      std::string filename;	// SPE output file name
      int bucketnum;		// number of buckets
      CGMP* gmp;		// GMP
      int key;			// client key
      int bucketid;		// bucket id
      int type;			// process type
      std::string function;     // Reduce operator
      char* param;              // Reduce parameter
      int psize;                // parameter size

      std::queue<Bucket>* bq;	// job queue for bucket data delivery
      pthread_mutex_t* bqlock;
      pthread_cond_t* bqcond;
      int64_t* pending;		// pending incoming data size
   };

   static void* fileHandler(void* p2);
   static void* copy(void* p3);
   static void* SPEHandler(void* p4);
   static void* SPEShuffler(void* p5);
   static void* SPEShufflerEx(void* p5);

private: // Sphere operations
   int SPEReadData(const std::string& datafile, const int64_t& offset, int& size, int64_t* index, const int64_t& totalrows, char*& block);
   int sendResultToFile(const SPEResult& result, const std::string& localfile, const int64_t& offset);
   int sendResultToBuckets(const int& speid, const int& buckets, const SPEResult& result, const SPEDestination& dest);
   int sendResultToClient(const int& buckets, const int* sarray, const int* rarray, const SPEResult& result, const std::string& clientip, int clientport, int session);

   int acceptLibrary(const int& key, const std::string& ip, int port, int session);
   int openLibrary(const int& key, const std::string& lib, void*& lh);
   int getSphereFunc(void* lh, const std::string& function, SPHERE_PROCESS& process);
   int getMapFunc(void* lh, const std::string& function, MR_MAP& map, MR_PARTITION& partition);
   int getReduceFunc(void* lh, const std::string& function, MR_COMPARE& compare, MR_REDUCE& reduce);
   int closeLibrary(void* lh);

   int sort(const std::string& bucket, MR_COMPARE comp, MR_REDUCE red);
   int reduce(std::vector<MRRecord>& vr, const std::string& bucket, MR_REDUCE red, void* param, int psize);

   int processData(SInput& input, SOutput& output, SFile& file, SPEResult& result, int buckets, SPHERE_PROCESS process, MR_MAP map, MR_PARTITION partition);
   int deliverResult(const int& buckets, const int& speid, SPEResult& result, SPEDestination& dest);

   int readSectorFile(const std::string& filename, const int64_t& offset, const int64_t& size, char* buf);

private: // SpaceDB operations
   int createTable(const std::string& name);
   int addTableAttribute(const std::string& name, const std::string& attr);
   int removeTableAttribute(const std::string& name, const std::string& attr);
   int deleteTable(const std::string& name);

private: // local FS operations
   int createDir(const std::string& path);
   int createSysDir();
   std::string reviseSysCmdPath(const std::string& path);
   int move(const std::string& src, const std::string& dst, const std::string& newname);

private: // local FS status
   int report(const std::string& master_ip, const int& master_port, const int32_t& transid, const std::string& path, const int32_t& change = 0);
   int report(const std::string& master_ip, const int& master_port, const int32_t& transid, const std::vector<std::string>& filelist, const int32_t& change = 0);
   int reportMO(const std::string& master_ip, const int& master_port, const int32_t& transid);
   int reportSphere(const std::string& master_ip, const int& master_port, const int32_t& transid, const std::vector<Address>* bad = NULL);

   int getFileList(const std::string& path, std::vector<std::string>& filelist);

   int checkBadDest(std::multimap<int64_t, Address>& sndspd, std::vector<Address>& bad);

private: // worker thread, report status, garbage collection, etc.
   static void* worker(void* param);

private:
   int m_iSlaveID;			// unique ID assigned by the master

   CGMP m_GMP;				// GMP messenger
   DataChn m_DataChn;			// data exchange channel
   int m_iDataPort;			// data channel port

   std::string m_strLocalHost;		// local host IP address
   int m_iLocalPort;			// local port number

   std::string m_strMasterHost;		// host name of the master node
   std::string m_strMasterIP;		// IP address of the master node
   int m_iMasterPort;			// port number of the master node

   std::string m_strHomeDir;     	// data directory
   time_t m_HomeDirMTime;		// last modified time

   std::string m_strBase;               // the local directory that stores Sector configuration files
   SlaveConf m_SysConfig;		// system configuration
   Metadata* m_pLocalFile;		// local file index
   SlaveStat m_SlaveStat;		// slave statistics
   SectorLog m_SectorLog;		// slave log

   MOMgmt m_InMemoryObjects;		// in-memory objects

   Routing m_Routing;			// master routing module

   TransManager m_TransManager;         // transaction management

private: //slave status
   bool m_bRunning;			// slave running status; used to terminate the slave when set to false

   pthread_mutex_t m_RunLock;
   pthread_cond_t m_RunCond;

   bool m_bDiskHealth;                  // disk health
   bool m_bNetworkHealth;               // network health
};

#endif