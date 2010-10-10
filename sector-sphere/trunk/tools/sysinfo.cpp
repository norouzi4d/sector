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
   Yunhong Gu, last updated 09/12/2010
*****************************************************************************/

#include <sector.h>
#include <iostream>

using namespace std;

string format(const int64_t& val)
{
   string fmt_val = "";

   int64_t left = val;
   while (left > 0)
   {
      int section = left % 1000;
      left = left / 1000;

      char buf[8];
      if (left > 0)
         sprintf(buf, "%03d", section);
      else
         sprintf(buf, "%d", section);

      if (fmt_val.c_str()[0] == 0)
         fmt_val = buf;
      else
         fmt_val = string(buf) + "," + fmt_val;
   }

   // nothing left, assign 0
   if (fmt_val.c_str()[0] == 0)
      fmt_val = "0";

   return fmt_val;
}

string format(const string& str, const int len)
{
   string fmt_str = str;

   for (int i = fmt_str.length(); i < len; ++ i)
      fmt_str += " ";

   return fmt_str;
}

string toString(const int64_t& val)
{
   char buf[64];
   sprintf(buf, "%lld", (long long)val);

   return buf;
}

string format(const int64_t& val, const int len)
{
   return format(toString(val), len);
}

string formatStatus(const int status, const int len)
{
   string info = "Unknown";

   switch (status)
   {
   case 0:
      info = "Down";
      break;
   case 1:
      info = "Normal";
      break;
   case 2:
      info = "DiskFull";
      break;
   case 3:
      return "Error";
      break;
   }

   if (len <= 0)
      return info;

   if (info.length() > unsigned(len))
      return info.substr(0, len);
   else
      info.append(len - info.length(), ' ');

   return info;
}

string formatSize(const int64_t& size)
{
   if (size <= 0)
      return "0";

   double k = 1000.;
   double m = 1000 * k;
   double g = 1000 * m;
   double t = 1000 * g;
   double p = 1000 * t;

   char sizestr[64];

   if (size < k)
      sprintf(sizestr, "%lld B", (long long int)size);
   else if (size < m)
      sprintf(sizestr, "%.3f KB", size / k);
   else if (size < g)
      sprintf(sizestr, "%.3f MB", size / m);
   else if (size < t)
      sprintf(sizestr, "%.3f GB", size / g);
   else if (size < p)
      sprintf(sizestr, "%.3f TB", size / t);
   else
      sprintf(sizestr, "%.3f PB", size / p);

   return sizestr;
}

void print(const SysStat& s)
{
   cout << "Sector System Information:" << endl;
   time_t st = s.m_llStartTime;
   cout << "Running since:               " << ctime(&st);
   cout << "Available Disk Size:         " << formatSize(s.m_llAvailDiskSpace)<< endl;
   cout << "Total File Size:             " << formatSize(s.m_llTotalFileSize) << endl;
   cout << "Total Number of Files:       " << s.m_llTotalFileNum << endl;
   cout << "Total Number of Slave Nodes: " << s.m_llTotalSlaves << endl;

   cout << "------------------------------------------------------------\n";
   cout << format("MASTER_ID", 10) << format("IP", 16) << "PORT" << endl;
   for (vector<SysStat::MasterStat>::const_iterator i = s.m_vMasterList.begin(); i != s.m_vMasterList.end(); ++ i)
   {
      cout << format(i->m_iID, 10) << format(i->m_strIP, 16) << i->m_iPort << endl;
   }

   cout << "------------------------------------------------------------\n";

   int total_cluster = 0;
   for (vector<SysStat::ClusterStat>::const_iterator i = s.m_vCluster.begin(); i != s.m_vCluster.end(); ++ i)
   {
      if (i->m_iTotalNodes > 0)
         ++ total_cluster;
   }

   cout << "Total number of clusters:    " << total_cluster << endl;
   cout << format("Cluster_ID", 12)
        << format("Total_Nodes", 12)
        << format("AvailDisk", 12)
        << format("FileSize", 12)
        << format("NetIn", 12)
        << format("NetOut", 12) << endl;
   for (vector<SysStat::ClusterStat>::const_iterator i = s.m_vCluster.begin(); i != s.m_vCluster.end(); ++ i)
   {
      if (i->m_iTotalNodes <= 0)
         continue;

      cout << format(i->m_iClusterID, 12)
           << format(i->m_iTotalNodes, 12)
           << format(formatSize(i->m_llAvailDiskSpace), 12)
           << format(formatSize(i->m_llTotalFileSize), 12)
           << format(formatSize(i->m_llTotalInputData), 12)
           << format(formatSize(i->m_llTotalOutputData), 12) << endl;
   }

   cout << "------------------------------------------------------------\n";
   cout << format("SLAVE_ID", 10)
        << format("Address", 24)
        << format("CLUSTER", 8)
        << format("STATUS", 10)
        << format("AvailDisk", 12)
        << format("TotalFile", 12)
        << format("Memory", 12)
        << format("CPU(us)", 12)
        << format("NetIn", 12)
        << format("NetOut", 12)
        << format("Data_Dir", 0) << endl;

   for (vector<SysStat::SlaveStat>::const_iterator i = s.m_vSlaveList.begin(); i != s.m_vSlaveList.end(); ++ i)
   {
      cout << format(i->m_iID, 10)
           << format(i->m_strIP + ":" + toString(i->m_iPort) , 24)
           << format(i->m_iClusterID, 8)
           << formatStatus(i->m_iStatus, 10)
           << format(formatSize(i->m_llAvailDiskSpace), 12)
           << format(formatSize(i->m_llTotalFileSize), 12)
           << format(formatSize(i->m_llCurrMemUsed), 12)
           << format(i->m_llCurrCPUUsed, 12)
           << format(formatSize(i->m_llTotalInputData), 12)
           << format(formatSize(i->m_llTotalOutputData), 12)
           << format(i->m_strDataDir, 0) << endl;
   }
}

int main(int argc, char** argv)
{
   cout << SectorVersion << endl;

   if (argc != 1)
   {
      cerr << "USAGE: sysinfo\n";
      return -1;
   }

   Sector client;
   if (Utility::login(client) < 0)
      return -1;

   SysStat sys;
   int result = client.sysinfo(sys);
   if (result >= 0)
      print(sys);
   else
      Utility::print_error(result);

   Utility::logout(client);

   return result;
}
