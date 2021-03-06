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


#ifndef __SECTOR_METADATA_H__
#define __SECTOR_METADATA_H__

#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>

#include "topology.h"
#include "sector.h"

#include "common.h"  // <slr>

class COMMON_API Metadata
{
public:
   Metadata();
   virtual ~Metadata();

   virtual void init(const std::string& path) = 0;
   virtual void clear() = 0;

public:	// list and lookup operations
   virtual int list(const std::string& path, std::vector<std::string>& filelist) = 0;
   virtual int list_r(const std::string& path, std::vector<std::string>& filelist) = 0;

      // Functionality:
      //    look up a specific file or directory in the metadata.
      // Parameters:
      //    [1] path: file or dir name
      //    [2] attr: SNode structure to store the information.
      // Returned value:
      //    If exist, 0 for a file, number of files or sub-dirs for a directory, or -1 on error.

   virtual int lookup(const std::string& path, SNode& attr) = 0;
   virtual int lookup(const std::string& path, std::set<Address, AddrComp>& addr) = 0;

public:	// update operations
   virtual int create(const SNode& node) = 0;
   virtual int move(const std::string& oldpath, const std::string& newpath, const std::string& newname = "") = 0;
   virtual int remove(const std::string& path, bool recursive = false) = 0;
   virtual int addReplica(const std::string& path, const int64_t& ts, const int64_t& size, const Address& addr) = 0;
   virtual int removeReplica(const std::string& path, const Address& addr) = 0;

      // Functionality:
      //    update the timestamp and size information of a file
      // Parameters:
      //    1) [in] path: file or dir name, full path
      //    2) [in] ts: timestamp
      //    3) [in] size: file size, when it is negative, ignore it and only update timestamp
      // Returned value:
      //    0 success, -1 error.

   virtual int update(const std::string& path, const int64_t& ts, const int64_t& size = -1) = 0;

public:	// lock/unlock
   virtual int lock(const std::string& path, int user, int mode);
   virtual int unlock(const std::string& path, int user, int mode);

public:	// serialization
   virtual int serialize(const std::string& path, const std::string& dstfile) = 0;
   virtual int deserialize(const std::string& path, const std::string& srcfile, const Address* addr) = 0;
   virtual int scan(const std::string& data_dir, const std::string& meta_dir) = 0;

public:	// medadata and file system operations

      // Functionality:
      //    merge a slave's index with the system file index.
      // Parameters:
      //    1) [in] path: merge into this director, usually "/"
      //    2) [in, out] branch: new metadata to be included; excluded/conflict metadata will be left, while others will be removed
      //    3) [in] replica: number of replicas
      // Returned value:
      //    0 on success, or -1 on error.

   virtual int merge(const std::string& path, Metadata* branch, const unsigned int& replica) = 0;

   virtual int substract(const std::string& path, const Address& addr) = 0;
   virtual int64_t getTotalDataSize(const std::string& path) = 0;
   virtual int64_t getTotalFileNum(const std::string& path) = 0;
   virtual int collectDataInfo(const std::string& path, std::vector<std::string>& result) = 0;
   virtual int checkReplica(const std::string& path, std::vector<std::string>& under, std::vector<std::string>& over, const unsigned int& thresh, const std::map<std::string, int>& special) = 0;

   virtual int getSlaveMeta(Metadata* branch, const Address& addr) = 0;

public:
   static int parsePath(const std::string& path, std::vector<std::string>& result);
   static std::string revisePath(const std::string& path);

protected:
   CMutex m_MetaLock;

   struct LockSet
   {
      std::set<int> m_sReadLock;
      std::set<int> m_sWriteLock;
   };
   std::map<std::string, LockSet> m_mLock;

private:
   static bool initLC();
   static bool m_pbLegalChar[256];
   static bool m_bInit;
};

#endif
