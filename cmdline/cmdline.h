// -*- mode: c++; mode: fold -*-
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/version.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/cmndline.h>
#include <apt-pkg/clean.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/srcrecords.h>

#include <fstream>
#include <stdio.h>

using std::ostream;
using std::ofstream;

static ostream c0out(0);
static ostream c1out(0);
static ostream c2out(0);
static ofstream devnull("/dev/null");
static unsigned int ScreenWidth = 80;

bool YnPrompt();
bool AnalPrompt(const char *Text);
void SigWinch(int);

const char *op2str(int op);

class cmdCacheFile : public pkgCacheFile
{
   static pkgCache *SortCache;
   static int NameComp(const void *a,const void *b);

   public:
   pkgCache::Package **List;
   void Sort();

   cmdCacheFile() : List(0) {};
};

class LogCleaner : public pkgArchiveCleaner
{
   protected:
   virtual void Erase(const char *File,string Pkg,string Ver,struct stat &St);

   public:
   virtual ~LogCleaner() {};
};


bool ShowList(ostream &out,string Title,string List,string VersionsList);
void Stats(ostream &out,pkgDepCache &Dep,pkgDepCache::State *State=NULL);
void ShowBroken(ostream &out,cmdCacheFile &Cache,bool Now, 
	        pkgDepCache::State *State=NULL);
void ShowNew(ostream &out,cmdCacheFile &Cache, pkgDepCache::State *State=NULL);
void ShowDel(ostream &out,cmdCacheFile &Cache, pkgDepCache::State *State=NULL);
void ShowKept(ostream &out,cmdCacheFile &Cache, pkgDepCache::State *State=NULL);
void ShowUpgraded(ostream &out,cmdCacheFile &Cache, 
		  pkgDepCache::State *State=NULL);
bool ShowDowngraded(ostream &out,cmdCacheFile &Cache, 
		    pkgDepCache::State *State=NULL);
bool ShowHold(ostream &out,cmdCacheFile &Cache, pkgDepCache::State *State=NULL);
bool ShowEssential(ostream &out,cmdCacheFile &Cache, pkgDepCache::State *State=NULL);

bool cmdDoClean(CommandLine &CmdL);

pkgSrcRecords::Parser *FindSrc(const char *Name,pkgRecords &Recs,
                               pkgSrcRecords &SrcRecs,string &Src,
                               pkgDepCache &Cache);


// vim:sts=3:sw=3
