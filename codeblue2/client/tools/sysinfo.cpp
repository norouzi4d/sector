#include <client.h>
#include <util.h>
#include <iostream>

using namespace std;

int main(int argc, char** argv)
{
   if (argc != 1)
   {
      cout << "USAGE: sysinfo\n";
      return -1;
   }

   Session s;
   s.loadInfo("../../conf/client.conf");

   if (Sector::init(s.m_ClientConf.m_strMasterIP, s.m_ClientConf.m_iMasterPort) < 0)
      return -1;
   if (Sector::login(s.m_ClientConf.m_strUserName, s.m_ClientConf.m_strPassword, s.m_ClientConf.m_strCertificate.c_str()) < 0)
      return -1;

   SysStat sys;
   Sector::sysinfo(sys);

   sys.print();

   Sector::logout();
   Sector::close();

   return 1;
}
