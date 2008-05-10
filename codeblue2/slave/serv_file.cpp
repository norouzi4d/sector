/*****************************************************************************
Copyright � 2006, 2007, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu [gu@lac.uic.edu], last updated 10/02/2007
*****************************************************************************/


#include <slave.h>
#include <iostream>

using namespace std;

void* Slave::fileHandler(void* p)
{
   Slave* self = ((Param2*)p)->serv_instance;
   string filename = self->m_strHomeDir + ((Param2*)p)->filename;
   Transport* datachn = ((Param2*)p)->datachn;
   string ip = ((Param2*)p)->client_ip;
   int port = ((Param2*)p)->client_data_port;
   int mode = ((Param2*)p)->mode;
   delete (Param2*)p;

   int32_t cmd;
   bool run = true;

   cout << "rendezvous connect " << ip << " " << port << " " << filename << endl;

   if (datachn->connect(ip.c_str(), port) < 0)
      return NULL;

   cout << "connected\n";

   timeval t1, t2;
   gettimeofday(&t1, 0);
   int64_t rb = 0;
   int64_t wb = 0;

   int32_t response = 0;

   while (run)
   {
      if (datachn->recv((char*)&cmd, 4) < 0)
         break;

      if (5 != cmd)
      {
         response = -1;

         if ((2 == cmd) || (4 == cmd))
         {
            if (0 != (mode & 2))
               response = 0;
         }
         else if ((1 == cmd) || (3 == cmd) || (6 == cmd))
         {
            if (0 != (mode & 1))
               response = 0;
         }

         if (datachn->send((char*)&response, 4) < 0)
            break;

         if (-1 == response)
            break;
      }

      switch (cmd)
      {
      case 1: // read
         {
            int64_t param[2];
            if (datachn->recv((char*)param, 8 * 2) < 0)
            {
               run = false;
               break;
            }

            ifstream ifs(filename.c_str(), ios::in | ios::binary);

            if (datachn->sendfile(ifs, param[0], param[1]) < 0)
               run = false;

            ifs.close();

            break;
         }

      case 6: // readridx
         {
            int64_t param[2];
            if (datachn->recv((char*)param, 8 * 2) < 0)
            {
               run = false;
               break;
            }

            ifstream ifs((filename + ".idx").c_str(), ios::in | ios::binary);

            if (datachn->sendfile(ifs, param[0] * 8, (param[1] + 1) * 8) < 0)
               run = false;

            ifs.close();

            break;
         }

      case 2: // write
         {
            int64_t param[2];

            if (datachn->recv((char*)param, 8 * 2) < 0)
            {
               run = false;
               break;
            }

            ofstream ofs;
            ofs.open(filename.c_str(), ios::out | ios::binary | ios::app);

            if (datachn->recvfile(ofs, param[0], param[1]) < 0)
               run = false;
            else
               wb += param[1];

            ofs.close();

            break;
         }

      case 3: // download
         {
            int64_t offset = 0;
            int64_t size = 0;

            if (datachn->recv((char*)&offset, 8) < 0)
            {
               run = false;
               break;
            }

            ifstream ifs(filename.c_str(), ios::in | ios::binary);
            ifs.seekg(0, ios::end);
            size = (int64_t)(ifs.tellg());
            ifs.seekg(0, ios::beg);

            size -= offset;

            if (datachn->send((char*)&size, 8) < 0)
            {
               run = false;
               ifs.close();
               break;
            }

            if (datachn->sendfile(ifs, offset, size) < 0)
               run = false;
            else
               rb += size;

            ifs.close();

            break;
         }

      case 4: // upload
         {
            int64_t offset = 0;
            int64_t size = 0;

            if (datachn->recv((char*)&size, 8) < 0)
            {
               run = false;
               break;
            }

            ofstream ofs(filename.c_str(), ios::out | ios::binary | ios::trunc);

            if (datachn->recvfile(ofs, offset, size) < 0)
               run = false;
            else
               wb += size;

            ofs.close();

            break;
         }

      case 5: // end session
         run = false;
         break;

      default:
         break;
      }
   }

   gettimeofday(&t2, 0);
   int duration = t2.tv_sec - t1.tv_sec;
   double avgRS = 0;
   double avgWS = 0;
   if (duration > 0)
   {
      avgRS = rb / duration * 8.0 / 1000000.0;
      avgWS = wb / duration * 8.0 / 1000000.0;
   }

   datachn->close();
   delete datachn;

   cout << "file server closed " << ip << " " << port << " " << avgRS << endl;

   return NULL;
}

void* Slave::copy(void* p)
{
   Slave* self = ((Param2*)p)->serv_instance;
   string filename = ((Param2*)p)->filename;
   delete (Param2*)p;

   SectorMsg msg;
   msg.setType(2); // open the file
   msg.setKey(0);
   msg.setData(0, filename.c_str(), filename.length() + 1);
   int32_t mode = 1;
   msg.setData(64, (char*)&mode, 4);

   Transport datachn;
   int port = 0;
   datachn.open(port);

   msg.setData(68, (char*)&port, 4);

   if (self->m_GMP.rpc(self->m_strMasterIP.c_str(), self->m_iMasterPort, &msg, &msg) < 0)
      return NULL;
   if (msg.getType() < 0)
      return NULL;

   cout << "rendezvous connect " << msg.getData() << " " << *(int*)(msg.getData() + 64) << endl;

   if (datachn.connect(msg.getData(), *(int*)(msg.getData() + 64)) < 0)
      return NULL;

   int32_t cmd = 3;
   int64_t offset = 0LL;
   int64_t size;
   int32_t response = -1;

   char req[12];
   *(int32_t*)req = cmd;
   *(int64_t*)(req + 4) = offset;

   if (datachn.send(req, 12) < 0)
      return NULL;
   if ((datachn.recv((char*)&response, 4) < 0) || (-1 == response))
      return NULL;
   if (datachn.recv((char*)&size, 8) < 0)
      return NULL;

   ofstream ofs;
   ofs.open((self->m_strHomeDir + filename).c_str(), ios::out | ios::binary | ios::trunc);
   if (datachn.recvfile(ofs, offset, size) < 0)
      unlink(filename.c_str());

   ofs.close();

   return NULL;
}