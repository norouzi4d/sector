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
   Yunhong Gu [gu@lac.uic.edu], last updated 03/06/2009
*****************************************************************************/


#include <ssltransport.h>
#include <netdb.h>
#include <crypto.h>
#include "client.h"
#include <iostream>

using namespace std;

string Client::g_strServerHost = "";
string Client::g_strServerIP = "";
int Client::g_iServerPort = 0;
CGMP Client::g_GMP;
DataChn Client::g_DataChn;
Topology Client::g_Topology;
SectorError Client::g_ErrorInfo;
int32_t Client::g_iKey = 0;
int Client::g_iCount = 0;
unsigned char Client::g_pcCryptoKey[16];
unsigned char Client::g_pcCryptoIV[8];

Client::Client()
{
}

Client::~Client()
{
}

int Client::init(const string& server, const int& port)
{
   if (g_iCount ++ > 0)
      return 1;

   g_ErrorInfo.init();

   struct hostent* serverip = gethostbyname(server.c_str());
   if (NULL == serverip)
   {
      cerr << "incorrect host name.\n";
      return -1;
   }
   g_strServerHost = server;
   char buf[64];
   g_strServerIP = inet_ntop(AF_INET, serverip->h_addr_list[0], buf, 64);
   g_iServerPort = port;

   Crypto::generateKey(g_pcCryptoKey, g_pcCryptoIV);

   Transport::initialize();
   if (g_GMP.init(0) < 0)
   {
      cerr << "unable to init GMP.\n ";
      return -1;
   }

   int dataport = 0;
   if (g_DataChn.init("", dataport) < 0)
   {
      cerr << "unable to init data channel.\n";
      return -1;
   }

   return 1;
}

int Client::login(const string& username, const string& password, const char* cert)
{
   SSLTransport::init();

   char* master_cert = (char*)cert;
   if (cert == NULL)
      master_cert = "master_node.cert";

   int result;
   SSLTransport secconn;
   if ((result = secconn.initClientCTX(master_cert)) < 0)
      return result;
   if ((result = secconn.open(NULL, 0)) < 0)
      return result;

   if ((result = secconn.connect(g_strServerHost.c_str(), g_iServerPort)) < 0)
   {
      cerr << "cannot set up secure connection to the master.\n";
      return result;
   }

   int cmd = 2;
   secconn.send((char*)&cmd, 4);

   // send username and password
   char buf[128];
   strncpy(buf, username.c_str(), 64);
   secconn.send(buf, 64);
   strncpy(buf, password.c_str(), 128);
   secconn.send(buf, 128);

   secconn.recv((char*)&g_iKey, 4);
   if (g_iKey < 0)
      return SectorError::E_SECURITY;

   int32_t port = g_GMP.getPort();
   secconn.send((char*)&port, 4);
   port = g_DataChn.getPort();
   secconn.send((char*)&port, 4);

   // send encryption key/iv
   secconn.send((char*)g_pcCryptoKey, 16);
   secconn.send((char*)g_pcCryptoIV, 8);

   int size = 0;
   secconn.recv((char*)&size, 4);
   if (size > 0)
   {
      char* tmp = new char[size];
      secconn.recv(tmp, size);
      g_Topology.deserialize(tmp, size);
   }

   secconn.close();
   SSLTransport::destroy();

   return g_iKey;
}

int Client::logout()
{
   SectorMsg msg;
   msg.setKey(g_iKey);
   msg.setType(2);
   msg.m_iDataLength = SectorMsg::m_iHdrSize;

   return g_GMP.rpc(g_strServerIP.c_str(), g_iServerPort, &msg, &msg);
}

int Client::close()
{
   if (g_iCount -- == 0)
   {
      g_strServerHost = "";
      g_strServerIP = "";
      g_iServerPort = 0;
      g_GMP.close();
      Transport::release();
   }

   return 1;
}

int Client::list(const string& path, vector<SNode>& attr)
{
   string revised_path = revisePath(path);

   SectorMsg msg;
   msg.resize(65536);
   msg.setType(101);
   msg.setKey(g_iKey);
   msg.setData(0, revised_path.c_str(), revised_path.length() + 1);

   if (g_GMP.rpc(g_strServerIP.c_str(), g_iServerPort, &msg, &msg) < 0)
      return SectorError::E_CONNECTION;

   if (msg.getType() < 0)
      return *(int32_t*)(msg.getData());

   string filelist = msg.getData();

   unsigned int s = 0;
   while (s < filelist.length())
   {
      int t = filelist.find(';', s);
      SNode sn;
      sn.deserialize(filelist.substr(s, t - s).c_str());
      attr.insert(attr.end(), sn);
      s = t + 1;
   }

   return attr.size();
}

int Client::stat(const string& path, SNode& attr)
{
   string revised_path = revisePath(path);

   SectorMsg msg;
   msg.resize(65536);
   msg.setType(102);
   msg.setKey(g_iKey);
   msg.setData(0, revised_path.c_str(), revised_path.length() + 1);

   if (g_GMP.rpc(g_strServerIP.c_str(), g_iServerPort, &msg, &msg) < 0)
      return SectorError::E_CONNECTION;

   if (msg.getType() < 0)
      return *(int32_t*)(msg.getData());

   attr.deserialize(msg.getData());

   int n = (msg.m_iDataLength - SectorMsg::m_iHdrSize - 128) / 68;
   char* al = msg.getData() + 128;

   for (int i = 0; i < n; ++ i)
   {
      Address addr;
      addr.m_strIP = al + 68 * i;
      addr.m_iPort = *(int32_t*)(al + 68 * i + 64);
      attr.m_sLocation.insert(addr);
   }

   return 1;
}

int Client::mkdir(const string& path)
{
   string revised_path = revisePath(path);

   SectorMsg msg;
   msg.setType(103);
   msg.setKey(g_iKey);
   msg.setData(0, revised_path.c_str(), revised_path.length() + 1);

   if (g_GMP.rpc(g_strServerIP.c_str(), g_iServerPort, &msg, &msg) < 0)
      return SectorError::E_CONNECTION;

   if (msg.getType() < 0)
      return *(int32_t*)(msg.getData());

   return 1;
}

int Client::move(const string& oldpath, const string& newpath)
{
   string src = revisePath(oldpath);
   string dst = revisePath(newpath);

   SectorMsg msg;
   msg.setType(104);
   msg.setKey(g_iKey);

   int32_t size = src.length() + 1;
   msg.setData(0, (char*)&size, 4);
   msg.setData(4, src.c_str(), src.length() + 1);
   size = dst.length() + 1;
   msg.setData(4 + src.length() + 1, (char*)&size, 4);
   msg.setData(4 + src.length() + 1 + 4, dst.c_str(), dst.length() + 1);

   if (g_GMP.rpc(g_strServerIP.c_str(), g_iServerPort, &msg, &msg) < 0)
      return SectorError::E_CONNECTION;

   if (msg.getType() < 0)
      return *(int32_t*)(msg.getData());

   return 1;
}

int Client::remove(const string& path)
{
   string revised_path = revisePath(path);

   SectorMsg msg;
   msg.setType(105);
   msg.setKey(g_iKey);
   msg.setData(0, revised_path.c_str(), revised_path.length() + 1);

   if (g_GMP.rpc(g_strServerIP.c_str(), g_iServerPort, &msg, &msg) < 0)
      return SectorError::E_CONNECTION;

   if (msg.getType() < 0)
      return *(int32_t*)(msg.getData());

   return 1;
}

int Client::copy(const string& src, const string& dst)
{
   string rsrc = revisePath(src);
   string rdst = revisePath(dst);

   SectorMsg msg;
   msg.setType(106);
   msg.setKey(g_iKey);

   int32_t size = rsrc.length() + 1;
   msg.setData(0, (char*)&size, 4);
   msg.setData(4, rsrc.c_str(), rsrc.length() + 1);
   size = rdst.length() + 1;
   msg.setData(4 + rsrc.length() + 1, (char*)&size, 4);
   msg.setData(4 + rsrc.length() + 1 + 4, rdst.c_str(), rdst.length() + 1);

   if (g_GMP.rpc(g_strServerIP.c_str(), g_iServerPort, &msg, &msg) < 0)
      return SectorError::E_CONNECTION;

   if (msg.getType() < 0)
      return *(int32_t*)(msg.getData());

   return 1;
}

string Client::revisePath(const string& path)
{
   if (path.c_str()[0] != '/')
      return "/" + path;

   return path;
}

int Client::sysinfo(SysStat& sys)
{
   SectorMsg msg;
   msg.setKey(g_iKey);
   msg.setType(3);
   msg.m_iDataLength = SectorMsg::m_iHdrSize;

   if (g_GMP.rpc(g_strServerIP.c_str(), g_iServerPort, &msg, &msg) < 0)
      return SectorError::E_CONNECTION;

   if (msg.getType() < 0)
      return *(int32_t*)(msg.getData());

   sys.deserialize(msg.getData(), msg.m_iDataLength);

   return 1;
}
