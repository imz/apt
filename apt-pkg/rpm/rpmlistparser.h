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
#include "rpmhandler.h"
#include <rpm/rpmlib.h>

#include <unordered_set>
#include <vector>
#include <regex.h>

using namespace std;

class RPMHandler;
class RPMPackageData;

class rpmListParser : public pkgCacheGenerator::ListParser
{
   RPMHandler *Handler;
   RPMPackageData *RpmData;

   string CurrentName;
   const pkgCache::VerIterator *VI;

   typedef std::unordered_set<std::string> SeenPackagesType;
   SeenPackagesType *SeenPackages;

   bool Duplicated;

   bool ParseStatus(pkgCache::PkgIterator Pkg,pkgCache::VerIterator Ver);
   bool ParseDepends(pkgCache::VerIterator Ver, unsigned int Type);
   bool ParseProvides(pkgCache::VerIterator Ver);

 public:

   // These all operate against the current header
   virtual string Package() override;
   virtual string Version() override;
   virtual string Architecture() override;
   virtual bool NewVersion(pkgCache::VerIterator Ver) override;
   virtual unsigned short VersionHash() override;
   virtual bool UsePackage(pkgCache::PkgIterator Pkg,
			   pkgCache::VerIterator Ver) override;
   virtual unsigned long Offset() override
	{return Handler->Offset();}
   virtual unsigned long Size() override;
   virtual unsigned long Flags() override;

   virtual bool OrderedOffset() override
	{return Handler->OrderedOffset();}

   virtual bool IsDatabase() override
	{return Handler->IsDatabase();}

   virtual bool CollectFileProvides(pkgCache &Cache,
				    pkgCache::VerIterator Ver) override;
   virtual bool Step() override;

   bool LoadReleaseInfo(pkgCache::PkgFileIterator FileI,FileFd &File);

   void VirtualizePackage(string Name);

   rpmListParser(RPMHandler *Handler);
   ~rpmListParser();
};

#endif
