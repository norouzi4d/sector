#include "speclient.h"

using namespace cb;

int main(int argc, char** argv)
{
   SPEClient sc;
   sc.connect(argv[1], atoi(argv[2]));

   STREAM s;
   s.m_strDataFile = "stream.dat";
   s.m_iUnitSize = 8;

   Process* myproc = sc.createJob();

   myproc->open(s, "myProc");
   myproc->run();

   while (true)
   {
      char* res;
      int size;
      if ((-1 == myproc->read(res, size)) || (0 == size))
         break;

      cout << "read one block " << size << endl;

      for (int i = 0; i < size; i += 4)
         cout << *(int*)(res + i) << endl;
   }

   myproc->close();
   sc.releaseJob(myproc);

   return 0;
}


// user defined process

//int myProc(const char* unit, const int& size, char* result, int& rsize, const char* param, const int& psize)
//{
//}