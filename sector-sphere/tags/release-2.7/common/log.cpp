/*****************************************************************************
Copyright 2005 - 2011 The Board of Trustees of the University of Illinois.

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
   Yunhong Gu, last updated 05/19/2011
*****************************************************************************/

#include <cstring>
#include <time.h>

#include <iostream>
#include <sstream>
#include <string>

#include "common.h"
#include "log.h"

#ifdef WIN32
   #define snprintf sprintf_s
   #define pthread_t int
   #define pthread_self() GetCurrentThreadId()
#endif

using namespace std;

LogStringTag::LogStringTag(const int tag, const int level):
m_iTag(tag),
m_iLevel(level),
m_strSrcFile(__FILE__),
m_iLine(__LINE__),
m_strFunc(__func__)
{
}


SectorLog::SectorLog():
m_iLevel(1),
m_iDay(-1),
m_bCopyToScreen(false)
{
}

SectorLog::~SectorLog()
{
}

int SectorLog::init(const char* path)
{
   // NULL path means users do not want to write to log file.
   if (NULL == path)
      return 0;
   m_strLogPath = path;

   string logfile;
   getTodayLog(m_iDay, logfile);

   m_LogFile.open((m_strLogPath + "/" + logfile).c_str(), ios::app);
   if (m_LogFile.bad() || m_LogFile.fail())
      return -1;

   return 0;
}

void SectorLog::close()
{
   m_LogFile.close();
}

void SectorLog::setLevel(const int level)
{
   if (level >= 0)
      m_iLevel = level;
}

void SectorLog::copyScreen(const bool screen)
{
   m_bCopyToScreen = screen;
}

SectorLog& SectorLog::operator<<(const LogStringTag& tag)
{
   CGuardEx lg(m_LogLock);

   pthread_t key = pthread_self();

   if (tag.m_iTag == LogTag::START)
   {
      LogString ls;
      ls.m_iLevel = tag.m_iLevel;
      m_mStoredString[key] = ls;
   }
   else if (tag.m_iTag == LogTag::END)
   {
      ThreadIdStringMap::iterator i = m_mStoredString.find(key);
      if (i != m_mStoredString.end())
      {
         insert_(i->second.m_strLog.c_str(), i->second.m_iLevel);
         m_mStoredString.erase(i);
      }
   }

   return *this;
}

SectorLog& SectorLog::operator<<(const std::string& message)
{
   CGuardEx lg(m_LogLock);

   pthread_t key = pthread_self();

   ThreadIdStringMap::iterator i = m_mStoredString.find(key);
   if (i == m_mStoredString.end())
   {
      // no start tag, use default: level = SCREEN
      LogString ls;
      ls.m_iLevel = LogLevel::SCREEN;
      m_mStoredString[key] = ls;
      i = m_mStoredString.find(key);
   }

   i->second.m_strLog += message;

   return *this;
}

SectorLog& SectorLog::operator<<(const int64_t& val)
{
   CGuardEx lg(m_LogLock);

   pthread_t key = pthread_self();

   ThreadIdStringMap::iterator i = m_mStoredString.find(key);
   if (i != m_mStoredString.end())
   {
      stringstream valstr;
      valstr << val;
      i->second.m_strLog += valstr.str();
   }

   return *this;
}

SectorLog& SectorLog::endl(SectorLog& log)
{
   CGuardEx lg(log.m_LogLock);
   pthread_t key = pthread_self();
   ThreadIdStringMap::iterator i = log.m_mStoredString.find(key);
   if (i != log.m_mStoredString.end())
   {
      log.insert_(i->second.m_strLog.c_str(), i->second.m_iLevel);
      log.m_mStoredString.erase(i);
   }

   return log;
}

void SectorLog::insert_(const char* text, const int level)
{
   if (level > m_iLevel)
      return;

   // Print this log to stdout.
   if (m_bCopyToScreen)
      cout << text << std::endl;

   if (m_LogFile.bad())
      return;

   checkLogFile();

   time_t t = time(NULL);
   char ct[64];
   snprintf(ct, 64, "%s", ctime(&t));
   ct[strlen(ct) - 1] = '\0';
   m_LogFile << ct << "\t" << "LEVEL: " << level << "\t" << text << std::endl;
   m_LogFile.flush();
}

void SectorLog::insert(const char* text, const int level)
{
   CGuardEx lg(m_LogLock);
   insert_(text, level);
}

void SectorLog::checkLogFile()
{
   int day = 0;
   string logfile;
   getTodayLog(day, logfile);

   if (day == m_iDay)
      return;

   // A new day starts.
   m_iDay = day;
   m_LogFile.close();
   m_LogFile.open((m_strLogPath + "/" + logfile).c_str(), ios::app);
}

void SectorLog::getTodayLog(int& day, string& file)
{
   time_t t = time(NULL);
   tm date;
#ifndef WIN32
   gmtime_r(&t, &date);
#else
   gmtime_s(&date, &t);
#endif

   day = date.tm_mday;

   stringstream fn;
   fn << date.tm_year + 1900 << ".";
   if (date.tm_mon >= 9)
      fn << date.tm_mon + 1 << ".";
   else
      fn << 0 << date.tm_mon + 1 << ".";
   if (date.tm_mday >= 10)
      fn << date.tm_mday << ".log";
   else
      fn << 0 << date.tm_mday << ".log";

   file = fn.str();
}
