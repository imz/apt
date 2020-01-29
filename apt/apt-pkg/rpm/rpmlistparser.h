// Description								/*{{{*/
// $Id: rpmlistparser.h,v 1.2 2002/07/26 17:39:28 niemeyer Exp $
/* ######################################################################
   
   RPM Package List Parser - This implements the abstract parser 
   interface for RPM package files
   
   ##################################################################### 
 */
									/*}}}*/
// Header section: pkglib
#ifndef PKGLIB_RPMLISTPARSER_H
#define PKGLIB_RPMLISTPARSER_H

#include <stdint.h>
#include <apt-pkg/pkgcachegen.h>
#include <apt-pkg/rpmhandler.h>
#include <apt-pkg/rpmmisc.h>
#include <rpm/rpmlib.h>
#include <map>
#include <vector>
#include <regex.h>
#include <experimental/optional>

using namespace std;

class RPMHandler;
class RPMPackageData;

class rpmListParser : public pkgCacheGenerator::ListParser
{
   RPMHandler *Handler;
   RPMPackageData *RpmData;
   Header header;

   string CurrentName;
   const pkgCache::VerIterator *VI;
   
#ifdef WITH_HASH_MAP
   typedef hash_map<const char*,bool,
   		    hash<const char*>,cstr_eq_pred> SeenPackagesType;
#else
   typedef map<const char*,bool,cstr_lt_pred> SeenPackagesType;
#endif
   SeenPackagesType *SeenPackages;

   bool Duplicated;
   
   std::experimental::optional<unsigned long> UniqFindTagWrite(int Tag);
   bool ParseStatus(pkgCache::PkgIterator &Pkg,pkgCache::VerIterator &Ver);
   bool ParseDepends(pkgCache::VerIterator &Ver,
		     char **namel, char **verl, int32_t *flagl,
		     int count, unsigned int Type);
   bool ParseDepends(pkgCache::VerIterator &Ver, unsigned int Type);
   bool ParseProvides(pkgCache::VerIterator &Ver);
   
#ifdef OLD_FILEDEPS
   bool ProcessFileProvides(pkgCache::VerIterator Ver);
#endif
   
 public:
   
   // These all operate against the current header
   virtual string Package() override;
   virtual string Version() override;
   virtual string Architecture() override;
   virtual bool NewVersion(pkgCache::VerIterator &Ver) override;
   virtual unsigned short VersionHash() override;
   virtual bool UsePackage(pkgCache::PkgIterator &Pkg,
			   pkgCache::VerIterator &Ver) override;
   virtual unsigned long long Offset() override
	{return Handler->Offset();};
   virtual unsigned long long Size() override;
   virtual unsigned long Flags() override;

   virtual bool OrderedOffset() override
   	{return Handler->OrderedOffset();};

   virtual bool IsDatabase() override
   	{return Handler->IsDatabase();};

   virtual bool CollectFileProvides(pkgCache &Cache,
				    pkgCache::VerIterator &Ver) override; 
   virtual bool Step() override;
   
   bool LoadReleaseInfo(pkgCache::PkgFileIterator &FileI,FileFd &File);

   void VirtualizePackage(const string &Name);
   
   rpmListParser(RPMHandler *Handler);
   ~rpmListParser();
};

#endif
