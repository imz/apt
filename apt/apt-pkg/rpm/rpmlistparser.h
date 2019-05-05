// -*- mode: cpp; mode: fold -*-
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
   
   unsigned long UniqFindTagWrite(int Tag);
   bool ParseStatus(pkgCache::PkgIterator Pkg,pkgCache::VerIterator Ver);
   bool ParseDepends(pkgCache::VerIterator Ver,
		     char **namel, char **verl, int32_t *flagl,
		     int count, unsigned int Type);
   bool ParseDepends(pkgCache::VerIterator Ver, unsigned int Type);
   bool ParseProvides(pkgCache::VerIterator Ver);
   
#ifdef OLD_FILEDEPS
   bool ProcessFileProvides(pkgCache::VerIterator Ver);
#endif
   
 public:
   
   // These all operate against the current header
   virtual string Package();
   virtual string Version();
   virtual string Architecture();
   virtual bool NewVersion(pkgCache::VerIterator Ver);
   virtual unsigned short VersionHash();
   virtual bool UsePackage(pkgCache::PkgIterator Pkg,
			   pkgCache::VerIterator Ver);
   virtual unsigned long Offset()
	{return Handler->Offset();};
   virtual unsigned long Size();

   virtual bool OrderedOffset()
   	{return Handler->OrderedOffset();};

   virtual bool IsDatabase()
   	{return Handler->IsDatabase();};

   virtual bool CollectFileProvides(pkgCache &Cache,
				    pkgCache::VerIterator Ver); 
   virtual bool Step();
   
   bool LoadReleaseInfo(pkgCache::PkgFileIterator FileI,FileFd &File);

   void VirtualizePackage(string Name);
   void CompatArchPackage(string Name);
   
   rpmListParser(RPMHandler *Handler);
   ~rpmListParser();
};

#endif
