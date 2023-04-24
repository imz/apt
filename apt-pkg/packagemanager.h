// Description								/*{{{*/
// $Id: packagemanager.h,v 1.1 2002/07/23 17:54:50 niemeyer Exp $
/* ######################################################################

   Package Manager - Abstacts the package manager

   Three steps are
     - Aquiration of archives (stores the list of final file names)
     - Sorting of operations
     - Invokation of package manager

   This is the final stage when the package cache entities get converted
   into file names and the state stored in a DepCache is transformed
   into a series of operations.

   In the final scheme of things this may serve as a director class to
   access the actual install methods based on the file type being
   installed.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_PACKAGEMANAGER_H
#define PKGLIB_PACKAGEMANAGER_H

#include <stdint.h>
#include <string>
#include <apt-pkg/pkgcache.h>

struct apt_item
{
   std::string file;
   uint32_t autoinstalled;

   apt_item(const std::string &l_file, uint32_t l_autoinstalled)
      : file(l_file),
      autoinstalled(l_autoinstalled)
   {
   }
};

using std::string;

class pkgAcquire;
class pkgDepCache;
class pkgSourceList;
class pkgOrderList;
class pkgRecords;

typedef enum aptCallbackType_e {
    APTCALLBACK_UNKNOWN = 0,
    APTCALLBACK_INST_PROGRESS,
    APTCALLBACK_INST_START,
    APTCALLBACK_INST_STOP,
    APTCALLBACK_TRANS_PROGRESS,
    APTCALLBACK_TRANS_START,
    APTCALLBACK_TRANS_STOP,
    APTCALLBACK_UNINST_PROGRESS,
    APTCALLBACK_UNINST_START,
    APTCALLBACK_UNINST_STOP,
    APTCALLBACK_UNPACK_ERROR,
    APTCALLBACK_CPIO_ERROR,
    APTCALLBACK_SCRIPT_ERROR,
    APTCALLBACK_SCRIPT_START,
    APTCALLBACK_SCRIPT_STOP,
    APTCALLBACK_ELEM_PROGRESS,
} aptCallbackType;

typedef void (*PackageManagerCallback_t)(const char *nevra,
                                         aptCallbackType what,
                                         uint64_t amount,
                                         uint64_t total,
                                         void *callbackData);

class pkgPackageManager : protected pkgCache::Namespace
{
   public:

   enum OrderResult {Completed,Failed,Incomplete};

   protected:
   string *FileNames;
   pkgDepCache &Cache;
   pkgOrderList *List;
   bool Debug;

   bool DepAdd(pkgOrderList &Order,PkgIterator P,int Depth = 0);
   virtual OrderResult OrderInstall();
   bool CheckRConflicts(PkgIterator Pkg,DepIterator Dep,const char *Ver);
   bool CreateOrderList();

   // Analysis helpers
   bool DepAlwaysTrue(DepIterator D);

   // Install helpers
   bool ConfigureAll();
   bool SmartConfigure(PkgIterator Pkg);
   bool SmartUnPack(PkgIterator Pkg);
   bool SmartRemove(PkgIterator Pkg);
   bool EarlyRemove(PkgIterator Pkg);

   // The Actual installation implementation
   virtual bool Install(PkgIterator /*Pkg*/,string /*File*/) = 0;
   virtual bool Configure(PkgIterator /*Pkg*/) = 0;
   virtual bool Remove(PkgIterator /*Pkg*/,bool /*Purge*/) = 0;
   virtual bool Go(PackageManagerCallback_t /*callback*/, void * /*callbackData*/) = 0;
   virtual void Reset() = 0;

   public:

   // Main action members
   bool GetArchives(pkgAcquire *Owner,pkgSourceList *Sources,
		    pkgRecords *Recs);
   OrderResult DoInstall(PackageManagerCallback_t callback = nullptr, void *callbackData = nullptr);
   bool FixMissing();

   // Also a part of the Actual installation implementation,
   // but it is public due to the use in apt-mark.
   // If marks updating not supported, skip this step
   virtual bool UpdateMarks() { return true; }

   pkgPackageManager(pkgDepCache *Cache);
   virtual ~pkgPackageManager();
};

#endif
