#include <fstream>
#include <fsclient.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <iostream>

using namespace std;

int upload(const char* file, const char* dst = NULL)
{
   timeval t1, t2;
   gettimeofday(&t1, 0);

   ifstream ifs(file);
   ifs.seekg(0, ios::end);
   long long int size = ifs.tellg();
   ifs.seekg(0);
   cout << "uploading " << file << " of " << size << " bytes" << endl;

   SectorFile f;

   char* rname;

   if (NULL != dst)
   {
      rname = (char*)dst;
   }
   else
   {
      rname = (char*)file;
      for (int i = strlen(file); i >= 0; -- i)
      {
         if ('/' == file[i])
         {
            rname = (char*)file + i + 1;
            break;
         }
      }
   }

   if (f.open(rname, SF_MODE::WRITE) < 0)
   {
      cout << "ERROR: unable to connect to server or file already exists." << endl;
      return -1;
   }

   bool finish = true;
   if (f.upload(file) < 0)
      finish = false;

   f.close();

   if (finish)
   {
      gettimeofday(&t2, 0);
      float throughput = size * 8.0 / 1000000.0 / ((t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0);

      cout << "Uploading accomplished! " << "AVG speed " << throughput << " Mb/s." << endl << endl ;
   }
   else
      cout << "Uploading failed! Please retry. " << endl << endl;

   return 1;
}

int main(int argc, char** argv)
{
   if ((4 != argc) && (5 != argc))
   {
      cout << "usage: upload <ip> <port> <src file> [dst file]" << endl;
      return 0;
   }

   Sector::init(argv[1], atoi(argv[2]));
   Sector::login("test", "xxx");

   if (5 == argc)
      upload(argv[3], argv[4]);
   else
      upload(argv[3]);

   Sector::logout();
   Sector::close();

   return 1;
}
