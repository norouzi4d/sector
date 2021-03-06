/*****************************************************************************
Copyright � 2006 - 2008, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu [gu@lac.uic.edu], last updated 02/18/2008
*****************************************************************************/


#ifndef __STORE_H__
#define __STORE_H__

#include <routing.h>
#include <gmp.h>
#include <transport.h>
#include <log.h>
#include <conf.h>
#include <index.h>
#include <kb.h>
#include <fs.h>

namespace cb
{

class Server;

class SPEResult
{
friend class Server;

public:
   ~SPEResult();

public:
   void init(const int& n);
   void addData(const int& bucketid, const int64_t* index, const int64_t& ilen, const char* data, const int64_t& dlen);
   void clear();

private:
   int m_iBucketNum;

   vector<int32_t> m_vIndexLen;
   vector<int64_t*> m_vIndex;
   vector<int32_t> m_vIndexPhyLen;
   vector<int32_t> m_vDataLen;
   vector<char*> m_vData;
   vector<int32_t> m_vDataPhyLen;
};


class Server
{
public:
   Server(const string& ip);
   ~Server();

public:
   int init(char* ip = NULL, int port = 0);
   int run();

private:
   struct Param1
   {
      Server* serv_instance;	// self
      string client_ip;		// client IP
      int client_ctrl_port;	// client control port
      int32_t msg_id;		// message ID
      CCBMsg* msg;		// message
   };

   struct Param2
   {
      Server* serv_instance;	// self
      string filename;		// filename
      Transport* datachn;	// data channel
      int mode;			// file access mode
      string client_ip;		// client IP
      int client_data_port;	// client data channel port
   };

   struct Param3
   {
      Server* serv_instance;	// self
      string filename;        	// filename
      string query;		// query
      Transport* datachn;	// data channel
      string client_ip;	        // client IP
      int client_data_port;     // client UDT port
   };

   struct Param4
   {
      Server* serv_instance;	// self
      Transport* datachn;	// data channel
      string client_ip;		// client IP
      int client_ctrl_port;	// client GMP port
      int client_data_port;	// client data port
      int speid;		// speid
      string function;		// SPE operator
      int rows;                 // number of rows per processing: -1 means all in the block
      char* param;		// SPE parameter
      int psize;		// parameter size
   };

   struct Param5
   {
      Server* serv_instance;    // self
      string client_ip;         // client IP
      int client_ctrl_port;     // client GMP port
      string filename;		// SPE output file name
      int dsnum;		// number of data segments (results to expect)
      CGMP* gmp;		// GMP
   };

   static void* process(void* s);
   static void* processEx(void* p1);
   static void* fileHandler(void* p2);
   static void* SPEHandler(void* p4);
   static void* SPEShuffler(void* p5);
   static void* createReplica(void* p1);

private:
   int SPEReadData(const string& datafile, const int64_t& offset, int& size, int64_t* index, const int64_t& totalrows, char*& block);
   int SPESendResult(const int& speid, const int& buckets, const SPEResult& result, const string& localfile, Transport* datachn, char* locations, map<Node, Transport*, NodeComp>* outputchn);

private:
   void updateOutLink();
   void updateInLink();

   int scanLocalFile();

private:
   CRouting* m_pRouter;

   CGMP m_GMP;

   LocalFileIndex m_LocalFile;
   RemoteFileIndex m_RemoteFile;

   string m_strLocalHost;
   int m_iLocalPort;

   static const int m_iKeySpace = 32;

   string m_strHomeDir;
   time_t m_HomeDirMTime;

   CAccessLog m_AccessLog;
   CPerfLog m_PerfLog;

   SECTORParam m_SysConfig;

   SectorFS m_SectorFS;

   KnowledgeBase m_KBase;

   timeval m_ReplicaCheckTime;
};

}; //namespace

#endif
