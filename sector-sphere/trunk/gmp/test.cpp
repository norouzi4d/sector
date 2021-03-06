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

#include <iostream>

#include "gmp.h"

using namespace std;

const int max_size = 2000;
const char server_ip[] = "127.0.0.1";
const int server_port = 2200;

#ifndef WIN32
void* Test_1_Srv(void*)
#else
DWORD WINAPI Test_1_Srv(LPVOID)
#endif
{
   CGMP gmp;
   gmp.init(server_port);

   string ip;
   int port;
   CUserMessage msg;
   msg.resize(max_size);
   int32_t id;
   const char* res = "got it.";

   strcpy(msg.m_pcBuffer, res);
   msg.m_iDataLength = strlen(res) + 1;

   // Receive all messages.
   for (int i = 0; i < 40; ++ i)
   {
      gmp.recvfrom(ip, port, id, &msg);
      cout << "UDP RECV " << ip << " " << port << " " << id << " " << msg.m_iDataLength << endl;
      strcpy(msg.m_pcBuffer, res);
      msg.m_iDataLength = strlen(res) + 1;
      gmp.sendto(ip, port, id, &msg);
   }

   cout << "server has received all messages.\n";
   gmp.close();

   return 0;
}

#ifndef WIN32
void* Test_1_Cli(void*)
#else
DWORD WINAPI Test_1_Cli(LPVOID)
#endif
{
   CGMP gmp;
   gmp.init(2210);

   cout << "RTT= " << gmp.rtt(server_ip, server_port) << endl;

   // Test small messages for UDP.
   CUserMessage req, res;
   req.resize(max_size);
   res.resize(max_size);
   req.m_iDataLength = 1000;
   int32_t id;

   for (int i = 0; i < 10; ++ i)
   {
      id = 0;
      gmp.sendto(server_ip, server_port, id, &req);
      res.m_pcBuffer[0] = 0;
      if (gmp.recv(id, &res) >= 0)
         cout << "UDP response: " << id << " " << res.m_pcBuffer << " " << res.m_iDataLength << " " << gmp.rtt(server_ip, server_port) << endl;
   }

   cout << "==========================================================\n";

   // Test large messages for UDT.
   req.m_iDataLength = 2000;
   for (int i = 0; i < 10; ++ i)
   {
      id = 0;
      gmp.sendto(server_ip, server_port, id, &req);
      res.m_pcBuffer[0] = 0;
      if (gmp.recv(id, &res) > 0)
         cout << "UDT response: " << id << " " << res.m_pcBuffer << " " << res.m_iDataLength << " " << gmp.rtt(server_ip, server_port) << endl;
   }

   cout << "client has sent all messages and received responsed.\n";
   gmp.close();

   cout << "==========================================================\n";

   // REPEAT: from a new gmp instance on the same port.
   gmp.init(2210);
   // Test large messages for UDT.
   req.m_iDataLength = 2000;
   for (int i = 0; i < 10; ++ i)
   {
      id = 0;
      gmp.sendto(server_ip, server_port, id, &req);
      res.m_pcBuffer[0] = 0;
      if (gmp.recv(id, &res) > 0)
         cout << "UDT response: " << id << " " << res.m_pcBuffer << " " << res.m_iDataLength << " " << gmp.rtt(server_ip, server_port) << endl;
   }
   gmp.close();

   cout << "==========================================================\n";

   // REPEAT: from a new gmp instance on a different port.
   gmp.init(2230);
   // Test large messages for UDT.
   req.m_iDataLength = 2000;
   for (int i = 0; i < 10; ++ i)
   {
      id = 0;
      gmp.sendto(server_ip, server_port, id, &req);
      res.m_pcBuffer[0] = 0;
      if (gmp.recv(id, &res) > 0)
         cout << "UDT response: " << id << " " << res.m_pcBuffer << " " << res.m_iDataLength << " " << gmp.rtt(server_ip, server_port) << endl;
   }
   gmp.close();

   cout << "==========================================================\n";

   return 0;
}

#ifndef WIN32
void* Test_2_Srv(void*)
#else
DWORD WINAPI Test_2_Srv(LPVOID)
#endif
{
   CGMP gmp;
   gmp.init(server_port);

   string ip;
   int port;
   CUserMessage msg;
   msg.resize(max_size);
   int32_t id;
   const char* res = "got it.";

   strcpy(msg.m_pcBuffer, res);
   msg.m_iDataLength = strlen(res) + 1;

   int local_chn = gmp.createChn();
   int remote_chn = 0;
   cout << "new channel: " << local_chn << endl;

   // read request on channel 0.
   gmp.recvfrom(ip, port, id, &msg, true);
   cout << "received request" << ip << " " << port << endl;
   *(int32_t*)msg.m_pcBuffer = local_chn;
   msg.m_iDataLength = 4;
   gmp.sendto(ip, port, id, &msg);

   // Receive all messages.
   for (int i = 0; i < 10; ++ i)
   {
      gmp.recvfrom(ip, port, id, &msg, true, &remote_chn, local_chn);
      cout << "SERV RECV " << ip << " " << port << " " << id << " " << msg.m_iDataLength << " " << remote_chn << endl;
      strcpy(msg.m_pcBuffer, res);
      msg.m_iDataLength = strlen(res) + 1;
      gmp.sendto(ip, port, id, &msg, local_chn, remote_chn);
   }

   cout << "server has received all messages.\n";
   gmp.close();

   return 0;
}

#ifndef WIN32
void* Test_2_Cli(void*)
#else
DWORD WINAPI Test_2_Cli(LPVOID)
#endif
{
   CGMP gmp;
   gmp.init(2210);

   cout << "RTT= " << gmp.rtt(server_ip, server_port) << endl;

   // Test small messages for UDP.
   CUserMessage req, res;
   req.resize(max_size);
   res.resize(max_size);
   req.m_iDataLength = 1000;
   int32_t id;
   int32_t remote_chn = 0;

   int local_chn = gmp.createChn();
   cout << "new channel: " << local_chn << endl;

   // request for new channel.
   id = 0;
   gmp.sendto(server_ip, server_port, id, &req);
   res.m_pcBuffer[0] = 0;
   if (gmp.recv(id, &res) >= 0)
   {
      remote_chn = *(int32_t*)res.m_pcBuffer;
      cout << "received channel = " << remote_chn << endl;
   }
   else
   {
      return 0;
   }

   // Test small messages.
   for (int i = 0; i < 0; ++ i)
   {
      id = 0;
      gmp.sendto(server_ip, server_port, id, &req, local_chn, remote_chn);
      res.m_pcBuffer[0] = 0;
      if (gmp.recv(id, &res, &remote_chn, local_chn) >= 0)
         cout << "UDP response: " << id << " " << res.m_pcBuffer << " " << res.m_iDataLength << " " << gmp.rtt(server_ip, server_port) << endl;
      else
         cout << "recv error\n";
   }

   // Test large messages.
   req.m_iDataLength = 2000;
   for (int i = 0; i < 10; ++ i)
   {
      id = 0;
      gmp.sendto(server_ip, server_port, id, &req, local_chn, remote_chn);
cout << "waiting for ID ============ " << id << endl;
      res.m_pcBuffer[0] = 0;
      if (gmp.recv(id, &res, &remote_chn, local_chn) >= 0)
         cout << "UDP response: " << id << " " << res.m_pcBuffer << " " << res.m_iDataLength << " " << gmp.rtt(server_ip, server_port) << endl;
      else
         cout << "recv error\n";
   }

   // start client, close, restart, check server state.
   return 0;
}

int main()
{
   const int test_case = 2;

#ifndef WIN32
   void* (*Test_Srv[test_case])(void*);
   void* (*Test_Cli[test_case])(void*);
#else
   DWORD (WINAPI *Test_Srv[test_case])(LPVOID);
   DWORD (WINAPI *Test_Cli[test_case])(LPVOID);
#endif

   Test_Srv[0] = Test_1_Srv;
   Test_Cli[0] = Test_1_Cli;
   Test_Srv[1] = Test_2_Srv;
   Test_Cli[1] = Test_2_Cli;

   for (int i = 0; i < test_case; ++ i)
   {
#ifndef WIN32
      pthread_t srv, cli;
      pthread_create(&srv, NULL, Test_Srv[i], NULL);
      pthread_create(&cli, NULL, Test_Cli[i], NULL);

      pthread_join(srv, NULL);
      pthread_join(cli, NULL);
#else
      HANDLE srv, cli;
      srv = CreateThread(NULL, 0, Test_Srv[i], NULL, 0, NULL);
      cli = CreateThread(NULL, 0, Test_Cli[i], NULL, 0, NULL);

      WaitForSingleObject(srv, INFINITE);
      WaitForSingleObject(cli, INFINITE);
#endif
   }

   return 0;
}
