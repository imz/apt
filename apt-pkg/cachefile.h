// Description								/*{{{*/
// $Id: cachefile.h,v 1.2 2002/07/25 18:07:18 niemeyer Exp $
/* ######################################################################

   CacheFile - Simple wrapper class for opening, generating and whatnot

   This class implements a simple 2 line mechanism to open various sorts
   of caches. It can operate as root, as not root, show progress and so on,
   it transparently handles everything necessary.

   This means it can rebuild caches from the source list and instantiates
   and prepares the standard policy mechanism.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_CACHEFILE_H
#define PKGLIB_CACHEFILE_H

#include <apt-pkg/depcache.h>
#include <apt-pkg/cmndline.h>

#include <memory>

class pkgPolicy;
class pkgSourceList;

class pkgCacheFile
{
   protected:

   std::unique_ptr<MMap> Map;
   pkgCache *Cache;
   pkgDepCache *DCache;
   pkgSourceList *SrcList;

   public:

   pkgPolicy *Policy;

   // We look pretty much exactly like a pointer to a dep cache
   inline operator pkgCache &() {return *Cache;}
   inline operator pkgCache *() {return Cache;}
   inline operator pkgDepCache &() {return *DCache;}
   inline operator pkgDepCache *() {return DCache;}
   inline operator pkgSourceList &() const {return *SrcList;}
   inline operator pkgSourceList *() const {return SrcList;}
   inline pkgDepCache *operator ->() {return DCache;}
   inline pkgDepCache &operator *() {return *DCache;}
   inline pkgDepCache::StateCache &operator [](pkgCache::PkgIterator const &I) {return (*DCache)[I];}
   inline unsigned char &operator [](pkgCache::DepIterator const &I) {return (*DCache)[I];}

   bool BuildCaches(OpProgress &Progress,bool WithLock);
   bool Open(OpProgress &Progress,bool WithLock);
   void Close();

   bool BuildSourceList(OpProgress *Progress = NULL);
   inline pkgSourceList* GetSourceList() { BuildSourceList(); return SrcList; }

   inline bool IsSrcListBuilt() const { return (SrcList != NULL); }

   static void RemoveCaches();

   pkgCacheFile();
   ~pkgCacheFile();
};

// class CacheFile - Cover class for some dependency cache functions	/*{{{*/
// ---------------------------------------------------------------------
/* */
class CacheFile: public pkgCacheFile
{
public:
   std::unique_ptr<pkgCache::Package*[]> List;

   explicit CacheFile(std::ostream &c1out);

   void Sort();

   // CacheFile::CheckDeps - Open the cache file
   // ---------------------------------------------------------------------
   /* This routine generates the caches and then opens the dependency cache
      and verifies that the system is OK. */
   bool CheckDeps(bool AllowBroken = false);
   bool BuildCaches(bool WithLock);
   bool Open(bool WithLock);
   bool OpenForInstall();

   bool CanCommit() const;

private:
   std::ostream &m_c1out;
   bool m_is_root;

   static pkgCache *SortCache;

   // CacheFile::NameComp - QSort compare by name
   static int NameComp(const void *a, const void *b);
};
									/*}}}*/

// ShowList - Show a list
// ---------------------------------------------------------------------
/* This prints out a string of space separated words with a title and
a two space indent line wraped to the current screen width. */
bool ShowList(std::ostream &out, const std::string &Title, std::string List, const std::string &VersionsList, size_t l_ScreenWidth);

// ShowBroken - Debugging aide
// ---------------------------------------------------------------------
/* This prints out the names of all the packages that are broken along
with the name of each each broken dependency and a quite version
description.

The output looks like:
The following packages have unmet dependencies:
exim: Depends: libc6 (>= 2.1.94) but 2.1.3-10 is to be installed
Depends: libldap2 (>= 2.0.2-2) but it is not going to be installed
Depends: libsasl7 but it is not going to be installed
*/
void ShowBroken(std::ostream &out, CacheFile &Cache, bool Now);

// ShowNew - Show packages to newly install
void ShowNew(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth);

// ShowDel - Show packages to delete
void ShowDel(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth);

// ShowKept - Show kept packages
void ShowKept(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth);

// ShowUpgraded - Show upgraded packages
void ShowUpgraded(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth);

// ShowDowngraded - Show downgraded packages
bool ShowDowngraded(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth);

// ShowHold - Show held but changed packages
bool ShowHold(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth);

// ShowEssential - Show an essential package warning
// ---------------------------------------------------------------------
/* This prints out a warning message that is not to be ignored. It shows
all essential packages and their dependents that are to be removed.
It is insanely risky to remove the dependents of an essential package! */
bool ShowEssential(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth);

// Stats - Show some statistics
void Stats(std::ostream &out, std::ostream &l_c3out, pkgDepCache &Dep, pkgDepCache::State *State);

bool pkgDoAuto(std::ostream &c1out, const CommandLine &CmdL, int &auto_mark_changed, pkgDepCache &Dep);
bool pkgDoShowAuto(std::ostream &cout, const CommandLine &CmdL, pkgDepCache &Dep);

#endif
