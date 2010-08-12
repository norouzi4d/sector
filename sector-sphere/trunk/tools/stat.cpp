#include <iostream>
#include <sector.h>
#include <conf.h>

using namespace std;

int main(int argc, char** argv)
{
   if (argc != 2)
   {
      cerr << "USAGE: stat file\n";
      return -1;
   }

   Sector client;

   Session s;
   s.loadInfo("../conf/client.conf");

   if (client.init(s.m_ClientConf.m_strMasterIP, s.m_ClientConf.m_iMasterPort) < 0)
      return -1;
   if (client.login(s.m_ClientConf.m_strUserName, s.m_ClientConf.m_strPassword, s.m_ClientConf.m_strCertificate.c_str()) < 0)
      return -1;

   SNode attr;
   int r = client.stat(argv[1], attr);

   if (r < 0)
   {
      cerr << "ERROR: " << r << " " << SectorError::getErrorMsg(r) << endl;
   }
   else
   {
      cout << "File Name: " << attr.m_strName << endl;
      if (attr.m_bIsDir)
         cout << "DIR: TRUE\n";
      else
         cout << "DIR: FALSE\n";
      cout << "Size: " << attr.m_llSize << " bytes" << endl;
      time_t ft = attr.m_llTimeStamp;
      cout << "Last Modified: " << ctime(&ft);
      cout << "Total Number of Replicas: " << attr.m_sLocation.size() << endl;
      cout << "Location:" << endl;
      for (set<Address, AddrComp>::iterator i = attr.m_sLocation.begin(); i != attr.m_sLocation.end(); ++ i)
      {
         cout << i->m_strIP << ":" << i->m_iPort << endl;
      }
   }

   client.logout();
   client.close();

   return r;
}
