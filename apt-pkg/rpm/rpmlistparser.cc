// Description
// $Id: rpmlistparser.cc,v 1.7 2003/01/29 18:55:03 niemeyer Exp $
//
/* ######################################################################
 *
 * Package Cache Generator - Generator for the cache structure.
 * This builds the cache structure from the abstract package list parser.
 *
 #####################################################################
 */


// Include Files
#include <config.h>

#ifdef HAVE_RPM

#define ALT_RPM_API
#include "rpmlistparser.h"
#include "rpmhandler.h"
#include "rpmpackagedata.h"
#include "rpmsystem.h"

#include <apt-pkg/error.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/crc-16.h>
#include <apt-pkg/tagfile.h>

#include <apti18n.h>

#include <rpm/rpmlib.h>

#include <rpm/rpmds.h>

#define WITH_VERSION_CACHING 1

string MultilibArchs[] = {"x86_64", "ia64", "ppc64", "sparc64"};

// ListParser::rpmListParser - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
rpmListParser::rpmListParser(RPMHandler *Handler)
	: Handler(Handler), VI(0)
{
   Handler->Rewind();
   header = NULL;
   if (Handler->IsDatabase() == true)
   {
      SeenPackages = new SeenPackagesType;
   }
   else
   {
      SeenPackages = NULL;
   }
   RpmData = RPMPackageData::Singleton();
}
                                                                        /*}}}*/

rpmListParser::~rpmListParser()
{
   delete SeenPackages;
}

// ListParser::Package - Return the package name			/*{{{*/
// ---------------------------------------------------------------------
/* This is to return the name of the package this section describes */
string rpmListParser::Package()
{
   if (CurrentName.empty() == false)
      return CurrentName;

#ifdef WITH_VERSION_CACHING
   if (VI != NULL) {
      CurrentName = VI->ParentPkg().Name();
      return CurrentName;
   }
#endif

   Duplicated = false;

   string Name = Handler->Name();
   if (Name.empty())
   {
      _error->Error(_("Corrupt pkglist: no RPMTAG_NAME in header entry"));
      return "";
   }

   bool IsDup = false;

   if (RpmData->IsMultilibSys() && RpmData->IsCompatArch(Architecture())) {
	 Name += ".32bit";
	 CurrentName = Name;
   }


   // If this package can have multiple versions installed at
   // the same time, then we make it so that the name of the
   // package is NAME+"#"+VERSION and also add a provides
   // with the original name and version, to satisfy the
   // dependencies.
   if (RpmData->IsDupPackage(Name) == true)
      IsDup = true;
   else if (SeenPackages != NULL) {
      if (SeenPackages->find(Name) != SeenPackages->end())
      {
	 if (_config->FindB("RPM::Allow-Duplicated-Warning", true) == true)
	    _error->Warning(
   _("There are multiple versions of \"%s\" in your system.\n"
     "\n"
     "This package won't be cleanly updated, unless you leave\n"
     "only one version. To leave multiple versions installed,\n"
     "you may remove that warning by setting the following\n"
     "option in your configuration file:\n"
     "\n"
     "RPM::Allow-Duplicated { \"^%s$\"; };\n"
     "\n"
     "To disable these warnings completely set:\n"
     "\n"
     "RPM::Allow-Duplicated-Warning \"false\";\n")
			      , Name.c_str(), Name.c_str());
	 RpmData->SetDupPackage(Name);
	 VirtualizePackage(Name);
	 IsDup = true;
      }
   }
   if (IsDup == true)
   {
      Name += "#"+Version();
      Duplicated = true;
   }
   CurrentName = Name;
   return Name;
}

                                                                        /*}}}*/
// ListParser::Arch - Return the architecture string			/*{{{*/
// ---------------------------------------------------------------------
string rpmListParser::Architecture()
{
#ifdef WITH_VERSION_CACHING
   if (VI != NULL)
      return VI->Arch();
#endif

   return Handler->Arch();
}
                                                                        /*}}}*/

#include <sstream>

// ListParser::Version - Return the version string			/*{{{*/
// ---------------------------------------------------------------------
/* This is to return the string describing the version in RPM form,
 version-release. If this returns the blank string then the
 entry is assumed to only describe package properties */
string rpmListParser::Version()
{
#ifdef WITH_VERSION_CACHING
   if (VI != NULL)
      return VI->VerStr();
#endif

   return Handler->EVRDB();
}
                                                                        /*}}}*/
// ListParser::NewVersion - Fill in the version structure		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool rpmListParser::NewVersion(pkgCache::VerIterator Ver)
{
#if WITH_VERSION_CACHING
   // Cache it for future usage.
   RpmData->SetVersion(Handler->GetID(), Offset(), Ver);
#endif

   // Parse the section
   Ver->Section = WriteUniqString(Handler->Group());
   Ver->Arch = WriteUniqString(Handler->Arch());

   // Archive Size
   Ver->Size = (unsigned long long)Handler->FileSize();

   // Unpacked Size (in kbytes)
   Ver->InstalledSize = Handler->InstalledSize();

   if (ParseDepends(Ver,pkgCache::Dep::Depends) == false)
       return false;
   if (ParseDepends(Ver,pkgCache::Dep::Conflicts) == false)
       return false;
   if (ParseDepends(Ver,pkgCache::Dep::Obsoletes) == false)
       return false;

   if (ParseProvides(Ver) == false)
       return false;

   if (Handler->ProvideFileName() &&
       NewProvides(Ver, Handler->FileName(), "") == false)
	 return false;

   return true;
}
									/*}}}*/
// ListParser::UsePackage - Update a package structure			/*{{{*/
// ---------------------------------------------------------------------
/* This is called to update the package with any new information
   that might be found in the section */
bool rpmListParser::UsePackage(pkgCache::PkgIterator Pkg,
			       pkgCache::VerIterator Ver)
{
   string PkgName = Pkg.Name();
   if (SeenPackages != NULL)
      SeenPackages->insert(PkgName);
   if (Pkg->Section == 0)
      Pkg->Section = WriteUniqString(Handler->Group());
   if (_error->PendingError())
       return false;
   string::size_type HashPos = PkgName.find('#');
   if (HashPos != string::npos)
      PkgName = PkgName.substr(0, HashPos);
   Ver->Priority = RpmData->VerPriority(PkgName);
   Pkg->Flags |= RpmData->PkgFlags(PkgName);
   if (HashPos != string::npos && (Pkg->Flags & pkgCache::Flag::Essential))
      Pkg->Flags = pkgCache::Flag::Important;
   if (ParseStatus(Pkg,Ver) == false)
       return false;
   return true;
}
                                                                        /*}}}*/
// ListParser::VersionHash - Compute a unique hash for this version	/*{{{*/
// ---------------------------------------------------------------------
/* */

/*
static int compare(const void *a, const void *b)
{
   return strcmp(*(char**)a, *(char**)b);
}
*/

unsigned short rpmListParser::VersionHash()
{
#ifdef WITH_VERSION_CACHING
   if (VI != NULL)
      return (*VI)->Hash;
#endif

   int Sections[] = {
	  RPMTAG_REQUIRENAME,
	  RPMTAG_OBSOLETENAME,
	  RPMTAG_CONFLICTNAME,
	  0
   };
   unsigned long Result = INIT_FCS;
   Result = AddCRC16(Result, Version());
   Result = AddCRC16(Result, Architecture());

   for (const int *sec = Sections; *sec != 0; sec++)
   {
      char *Str;
      int Len;
      rpm_tagtype_t type;
      rpm_count_t count;
      int res;
      char **strings = NULL;

      res = headerGetEntry(header, *sec, &type, (void **)&strings, &count);
      if (res != 1)
	 continue;

      switch (type)
      {
      case RPM_STRING_ARRAY_TYPE:
	 //qsort(strings, count, sizeof(char*), compare);
	 while (count-- > 0)
	 {
	    Str = strings[count];
	    Len = strlen(Str);

	    /* Suse patch.rpm hack. */
	    if (Len == 17 && *Str == 'r' && *sec == RPMTAG_REQUIRENAME &&
	        strcmp(Str, "rpmlib(PatchRPMs)") == 0)
	       continue;

	    Result = AddCRC16(Result,Str,Len);
	 }
	 free(strings);
	 break;

      case RPM_STRING_TYPE:
	 Str = (char*)strings;
	 Len = strlen(Str);
	 Result = AddCRC16(Result,Str,Len);
	 break;
      }
   }

   return Result;
}
                                                                        /*}}}*/
// ListParser::ParseStatus - Parse the status field			/*{{{*/
// ---------------------------------------------------------------------
bool rpmListParser::ParseStatus(pkgCache::PkgIterator Pkg,
				pkgCache::VerIterator Ver)
{
   if (!Handler->IsDatabase())  // this means we're parsing an hdlist, so it's not installed
      return true;

   // if we're reading from the rpmdb, then it's installed
   //
   Pkg->SelectedState = pkgCache::State::Install;
   Pkg->InstState = pkgCache::State::Ok;
   Pkg->CurrentState = pkgCache::State::Installed;

   Pkg->CurrentVer = Ver.Index();

   return true;
}


bool rpmListParser::ParseDepends(pkgCache::VerIterator Ver,
				 char **namel, char **verl, int32_t *flagl,
				 int count, unsigned int Type)
{
   int i = 0;
   unsigned int Op = 0;
   bool DepMode = false;
   if (Type == pkgCache::Dep::Depends)
      DepMode = true;

   for (; i < count; i++)
   {
      if (DepMode == true) {
	 if (flagl[i] & RPMSENSE_PREREQ)
	    Type = pkgCache::Dep::PreDepends;
	 else if (flagl[i] & RPMSENSE_MISSINGOK)
	    Type = pkgCache::Dep::Suggests;
	 else
	    Type = pkgCache::Dep::Depends;
      }

      if (namel[i][0] == 'r' && strncmp(namel[i], "rpmlib", 6) == 0)
      {
	 /* 4.13.0 (ALT specific) */
	 int res = rpmCheckRpmlibProvides(namel[i], verl?verl[i]:NULL,
					  flagl[i]);
	 if (res) continue;
      }

      if (verl)
      {
	 if (!*verl[i])
	 {
	    Op = pkgCache::Dep::NoOp;
	 }
	 else
	 {
	    if (flagl[i] & RPMSENSE_LESS)
	    {
	       if (flagl[i] & RPMSENSE_EQUAL)
		   Op = pkgCache::Dep::LessEq;
	       else
		   Op = pkgCache::Dep::Less;
	    }
	    else if (flagl[i] & RPMSENSE_GREATER)
	    {
	       if (flagl[i] & RPMSENSE_EQUAL)
		   Op = pkgCache::Dep::GreaterEq;
	       else
		   Op = pkgCache::Dep::Greater;
	    }
	    else if (flagl[i] & RPMSENSE_EQUAL)
	    {
	       Op = pkgCache::Dep::Equals;
	    }
	 }

	 if (NewDepends(Ver,namel[i],verl[i],Op,Type) == false)
	     return false;
      }
      else
      {
	 if (NewDepends(Ver,namel[i],"",pkgCache::Dep::NoOp,Type) == false)
	     return false;
      }
   }
   return true;
}
                                                                        /*}}}*/
// ListParser::ParseDepends - Parse a dependency list			/*{{{*/
// ---------------------------------------------------------------------
/* This is the higher level depends parser. It takes a tag and generates
 a complete depends tree for the given version. */
bool rpmListParser::ParseDepends(pkgCache::VerIterator Ver,
				 unsigned int Type)
{
   char **namel = NULL;
   char **verl = NULL;
   int *flagl = NULL;
   int res;
   rpm_tagtype_t type;
   rpm_count_t count;

   switch (Type)
   {
   case pkgCache::Dep::Depends:
      res = headerGetEntry(header, RPMTAG_REQUIRENAME, &type,
			   (void **)&namel, &count);
      if (res != 1)
	  return true;
      res = headerGetEntry(header, RPMTAG_REQUIREVERSION, &type,
			   (void **)&verl, &count);
      res = headerGetEntry(header, RPMTAG_REQUIREFLAGS, &type,
			   (void **)&flagl, &count);
      break;

   case pkgCache::Dep::Obsoletes:
      res = headerGetEntry(header, RPMTAG_OBSOLETENAME, &type,
			   (void **)&namel, &count);
      if (res != 1)
	  return true;
      res = headerGetEntry(header, RPMTAG_OBSOLETEVERSION, &type,
			   (void **)&verl, &count);
      res = headerGetEntry(header, RPMTAG_OBSOLETEFLAGS, &type,
			   (void **)&flagl, &count);
      break;

   case pkgCache::Dep::Conflicts:
      res = headerGetEntry(header, RPMTAG_CONFLICTNAME, &type,
			   (void **)&namel, &count);
      if (res != 1)
	  return true;
      res = headerGetEntry(header, RPMTAG_CONFLICTVERSION, &type,
			   (void **)&verl, &count);
      res = headerGetEntry(header, RPMTAG_CONFLICTFLAGS, &type,
			   (void **)&flagl, &count);
      break;
   }

   ParseDepends(Ver, namel, verl, flagl, count, Type);

   free(namel);
   free(verl);

   return true;
}
                                                                        /*}}}*/
bool rpmListParser::CollectFileProvides(pkgCache &Cache,
					pkgCache::VerIterator Ver)
{
   bool ret = true;
   const char *FileName;
   rpmtd fileNames = rpmtdNew();

   if (headerGet(header, RPMTAG_OLDFILENAMES, fileNames, HEADERGET_EXT) != 1 &&
       headerGet(header, RPMTAG_FILENAMES, fileNames, HEADERGET_EXT) != 1) {
      rpmtdFree(fileNames);
      return true;
   }
   while ((FileName = rpmtdNextString(fileNames)) != NULL) {
      pkgCache::Package *P = Cache.FindPackage(FileName);
      if (P != NULL) {
	 // Check if this is already provided.
	 bool Found = false;
	 for (pkgCache::PrvIterator Prv = Ver.ProvidesList();
	      Prv.end() == false; Prv++) {
	    if (strcmp(Prv.Name(), FileName) == 0) {
	       Found = true;
	       break;
	    }
	 }
	 if (Found == false && NewProvides(Ver, FileName, "") == false) {
	    ret = false;
	    break;
	 }
      }
   }
   rpmtdFreeData(fileNames);
   rpmtdFree(fileNames);
   return ret;
}

// ListParser::ParseProvides - Parse the provides list			/*{{{*/
// ---------------------------------------------------------------------
/* */
bool rpmListParser::ParseProvides(pkgCache::VerIterator Ver)
{
   rpm_tagtype_t type;
   rpm_count_t count;
   char **namel = NULL;
   char **verl = NULL;
   int res;

   res = headerGetEntry(header, RPMTAG_PROVIDENAME, &type,
			(void **)&namel, &count);
   if (res != 1)
       return true;
   /*
    res = headerGetEntry(header, RPMTAG_PROVIDEFLAGS, &type,
    (void **)&flagl, &count);
    if (res != 1)
    return true;
    */
   res = headerGetEntry(header, RPMTAG_PROVIDEVERSION, &type,
			(void **)&verl, NULL);
   if (res != 1)
	verl = NULL;

   bool ret = true;
   for (int i = 0; i < count; i++)
   {
      if (verl && *verl[i])
      {
	 if (NewProvides(Ver,namel[i],verl[i]) == false) {
	    ret = false;
	    break;
	 }
      }
      else
      {
	 if (NewProvides(Ver,namel[i],"") == false) {
	    ret = false;
	    break;
	 }
      }
   }

   free(namel);
   free(verl);

   return ret;
}
                                                                        /*}}}*/
// ListParser::Step - Move to the next section in the file		/*{{{*/
// ---------------------------------------------------------------------
/* This has to be carefull to only process the correct architecture */
bool rpmListParser::Step()
{
   while (Handler->Skip() == true)
   {
      header = Handler->GetHeader();
      CurrentName = "";

#ifdef WITH_VERSION_CACHING
      VI = RpmData->GetVersion(Handler->GetID(), Offset());
      if (VI != NULL)
	 return true;
#endif

      string RealName = Package();

      if (Duplicated == true)
	 RealName = RealName.substr(0,RealName.find('#'));
      if (RpmData->IgnorePackage(RealName) == true)
	 continue;

      if (Handler->IsDatabase() == true ||
	  RpmData->ArchScore(Architecture()) > 0)
	 return true;
   }
   header = NULL;
   return false;
}
                                                                        /*}}}*/
// ListParser::LoadReleaseInfo - Load the release information		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool rpmListParser::LoadReleaseInfo(pkgCache::PkgFileIterator FileI,
				    FileFd &File)
{
   pkgTagFile Tags(&File);
   pkgTagSection Section;
   if (!Tags.Step(Section))
       return false;

   const char *Start;
   const char *Stop;
   if (Section.Find("Archive",Start,Stop))
       FileI->Archive = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Component",Start,Stop))
       FileI->Component = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Version",Start,Stop))
       FileI->Version = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Origin",Start,Stop))
       FileI->Origin = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Label",Start,Stop))
       FileI->Label = WriteUniqString(Start,Stop - Start);
   if (Section.Find("Architecture",Start,Stop))
       FileI->Architecture = WriteUniqString(Start,Stop - Start);

   if (Section.FindFlag("NotAutomatic",FileI->Flags,
			pkgCache::Flag::NotAutomatic) == false)
       _error->Warning(_("Bad NotAutomatic flag"));

   return !_error->PendingError();
}
                                                                        /*}}}*/

unsigned long rpmListParser::Size()
{
   return (Handler->InstalledSize() + 512) / 1024;
}

unsigned long rpmListParser::Flags()
{
   return Handler->AutoInstalled() ? pkgCache::Flag::Auto : 0;
}

// This is a slightly complex operation. It must take a package, and
// move every version to new packages, named accordingly to
// Allow-Duplicated rules.
void rpmListParser::VirtualizePackage(string Name)
{
   pkgCache::PkgIterator FromPkgI = Owner->GetCache().FindPkg(Name);

   // Should always be false
   if (FromPkgI.end() == true)
      return;

   pkgCache::VerIterator FromVerI = FromPkgI.VersionList();
   while (FromVerI.end() == false) {
      string MangledName = Name+"#"+string(FromVerI.VerStr());

      // Get the new package.
      pkgCache::PkgIterator ToPkgI = Owner->GetCache().FindPkg(MangledName);
      if (ToPkgI.end() == true) {
	 // Theoretically, all packages virtualized should pass here at least
	 // once for each new version in the list, since either the package was
	 // already setup as Allow-Duplicated (and this method would never be
	 // called), or the package doesn't exist before getting here. If
	 // we discover that this assumption is false, then we must do
	 // something to order the version list correctly, since the package
	 // could already have some other version there.
	 Owner->NewPackage(ToPkgI, MangledName);

	 // Should it get the flags from the original package? Probably not,
	 // or automatic Allow-Duplicated would work differently than
	 // hardcoded ones.
	 ToPkgI->Flags |= RpmData->PkgFlags(MangledName);
	 ToPkgI->Section = FromPkgI->Section;
      }

      // Move the version to the new package.
      FromVerI->ParentPkg = ToPkgI.Index();

      // Put it at the end of the version list (about ordering,
      // read the comment above).
      map_ptrloc *ToVerLast = &ToPkgI->VersionList;
      for (pkgCache::VerIterator ToVerLastI = ToPkgI.VersionList();
	   ToVerLastI.end() == false; ToVerLastI++)
	   ToVerLast = &ToVerLastI->NextVer;

      *ToVerLast = FromVerI.Index();

      // Provide the real package name with the current version.
      NewProvides(FromVerI, Name, FromVerI.VerStr());

      // Is this the current version of the old package? If yes, set it
      // as the current version of the new package as well.
      if (FromVerI == FromPkgI.CurrentVer()) {
	 ToPkgI->CurrentVer = FromVerI.Index();
	 ToPkgI->SelectedState = pkgCache::State::Install;
	 ToPkgI->InstState = pkgCache::State::Ok;
	 ToPkgI->CurrentState = pkgCache::State::Installed;
      }

      // Move the iterator before reseting the NextVer.
      pkgCache::Version *FromVer = (pkgCache::Version*)FromVerI;
      FromVerI++;
      FromVer->NextVer = 0;
   }

   // Reset original package data.
   FromPkgI->CurrentVer = 0;
   FromPkgI->VersionList = 0;
   FromPkgI->Section = 0;
   FromPkgI->SelectedState = 0;
   FromPkgI->InstState = 0;
   FromPkgI->CurrentState = 0;
}

#endif /* HAVE_RPM */

// vim:sts=3:sw=3
