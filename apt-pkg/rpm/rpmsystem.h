// Description								/*{{{*/
// $Id: rpmsystem.h,v 1.2 2002/07/30 20:43:41 niemeyer Exp $
/* ######################################################################

   System - RPM version of the  System Class

   #####################################################################
 */
									/*}}}*/
#ifndef PKGLIB_RPMSYSTEM_H
#define PKGLIB_RPMSYSTEM_H

#include "rpmindexfile.h"
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/algorithms.h>

#include <map>

class RPMDBHandler;
class pkgSourceList;
class rpmIndexFile;

class rpmSystem : public pkgSystem
{
   int LockCount;
   RPMDBHandler *RpmDB;
   rpmDatabaseIndex *StatusFile;

   bool processIndexFile(rpmIndexFile *Handler,OpProgress &Progress);

   public:

   RPMDBHandler *GetDBHandler();

   virtual bool LockRead() override;
   virtual bool Lock() override;
   virtual bool UnLock(bool NoErrors = false) override;
   virtual pkgPackageManager *CreatePM(pkgDepCache *Cache) const override;
   virtual bool Initialize(Configuration &Cnf) override;
   virtual bool ArchiveSupported(const char *Type) override;
   virtual signed Score(Configuration const &Cnf) override;
   virtual string DistroVer() override;
   virtual bool AddStatusFiles(vector<pkgIndexFile *> &List) override;
   virtual void AddSourceFiles(vector<pkgIndexFile *> &List) override;
   virtual bool FindIndex(pkgCache::PkgFileIterator File,
			  pkgIndexFile *&Found) const override;
   virtual bool ProcessCache(pkgDepCache &Cache,pkgProblemResolver &Fix) override;
   virtual bool IgnoreDep(pkgVersioningSystem &VS,pkgCache::DepIterator &Dep) override;
   virtual void CacheBuilt() override;

   virtual unsigned long OptionsHash() const override;

   rpmSystem();
   virtual ~rpmSystem();
};

extern rpmSystem rpmSys;

#endif
