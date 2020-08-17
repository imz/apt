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
#include <apt-pkg/rpmlistparser.h>
#include <apt-pkg/rpmhandler.h>
#include <apt-pkg/rpmpackagedata.h>
#include <apt-pkg/rpmsystem.h>
#include <apt-pkg/error.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/crc-16.h>
#include <apt-pkg/tagfile.h>

#include <apti18n.h>

#include <rpm/rpmlib.h>

#if RPM_VERSION >= 0x040100
#include <rpm/rpmds.h>
#endif

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
#ifdef WITH_HASH_MAP
      SeenPackages = new SeenPackagesType(517);
#else
      SeenPackages = new SeenPackagesType;
#endif
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

// ListParser::UniqFindTagWrite - Find the tag and write a unq string	/*{{{*/
// ---------------------------------------------------------------------
/* */
std::optional<unsigned long> rpmListParser::UniqFindTagWrite(int Tag)
{
   char *Start;
   char *Stop;
   rpm_tagtype_t type;
   rpm_count_t count;
   void *data;
   
   if (headerGetEntry(header, Tag, &type, &data, &count) != 1)
   {
      /*
       * Some 3rd-party rpm packages may not contain these tags.
       * But since cacheiterators treat zero as special value,
       * just pass it instead of failing
       */
      return 0;
   }
   
   if (type == RPM_STRING_TYPE) 
   {
      Start = (char*)data;
      Stop = Start + strlen(Start);
   } else {
      cout << "oh shit, not handled:"<<type<<" Package:"<<Package()<<endl;
      abort();
   }
   
   return WriteUniqString(Start,Stop - Start);
}

                                                                        /*}}}*/
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

   char *str;
   rpm_tagtype_t type;
   rpm_count_t count;
   
   Duplicated = false;
   
   if (headerGetEntry(header, RPMTAG_NAME, &type, (void**)&str, &count) != 1) 
   {
      _error->Error(_("Corrupt pkglist: no RPMTAG_NAME in header entry"));
      return "";
   } 

   bool IsDup = false;
   string Name = str;

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
      if (SeenPackages->find(Name.c_str()) != SeenPackages->end())
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

   rpm_tagtype_t type;
   rpm_count_t count;
   char *arch;
   int res;
   res = headerGetEntry(header, RPMTAG_ARCH, &type, (void **)&arch, &count);
   return string(res?arch:"");
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

   char *ver, *rel, *dtag;
   bool has_dtag = false;
   uint32_t *ser;
   bool has_epoch = false;
   uint32_t *btime;
   bool has_btime = false;
   rpm_tagtype_t type;
   rpm_count_t count;
   stringstream ss;

   if (headerGetEntry(header, RPMTAG_EPOCH, &type, (void **)&ser, &count) == 1
       && count > 0) 
      has_epoch = true;
   
   headerGetEntry(header, RPMTAG_VERSION, &type, (void **)&ver, &count);
   headerGetEntry(header, RPMTAG_RELEASE, &type, (void **)&rel, &count);

   if (headerGetEntry(header, RPMTAG_DISTTAG, &type, (void **)&dtag, &count) == 1
       && count > 0)
      has_dtag = true;

   if (headerGetEntry(header, RPMTAG_BUILDTIME, &type, (void **)&btime, &count) == 1
       && count > 0)
      has_btime = true;

   if (has_epoch) {
      ss << ser[0] << ":";
   }
   ss << ver << "-" << rel;
   if (has_dtag) {
      ss << ":" << dtag;
   }
   if (has_btime) {
      ss << "@" << btime[0];
   }
   return ss.str();
}
                                                                        /*}}}*/
// ListParser::NewVersion - Fill in the version structure		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool rpmListParser::NewVersion(pkgCache::VerIterator &Ver)
{
   rpm_tagtype_t type;
   rpm_count_t count;
   int32_t *num;

#if WITH_VERSION_CACHING
   // Cache it for future usage.
   RpmData->SetVersion(Handler->GetID(), Offset(), Ver);
#endif
   
   // Parse the section
   const auto idxSection = UniqFindTagWrite(RPMTAG_GROUP);
   const auto idxArch = UniqFindTagWrite(RPMTAG_ARCH);
   if ((!idxSection) || (!idxArch))
      return false;

   Ver->Section = *idxSection;
   Ver->Arch = *idxArch;
   
   // Archive Size
   Ver->Size = Handler->FileSize();
   
   // Unpacked Size (in kbytes)
   headerGetEntry(header, RPMTAG_SIZE, &type, (void**)&num, &count);
   Ver->InstalledSize = (unsigned long long)num[0];
     
   if (ParseDepends(Ver,pkgCache::Dep::Depends) == false)
       return false;
   if (ParseDepends(Ver,pkgCache::Dep::Conflicts) == false)
       return false;
   if (ParseDepends(Ver,pkgCache::Dep::Obsoletes) == false)
       return false;
#ifdef OLD_FILEDEPS
   if (ProcessFileProvides(Ver) == false)
       return false;
#endif

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
bool rpmListParser::UsePackage(pkgCache::PkgIterator &Pkg,
			       pkgCache::VerIterator &Ver)
{
   if (SeenPackages != NULL)
      (*SeenPackages)[Pkg.Name()] = true;
   if (Pkg->Section == 0)
   {
      const auto idxSection = UniqFindTagWrite(RPMTAG_GROUP);
      if (!idxSection)
         return false;

      Pkg->Section = *idxSection;
   }

   if (_error->PendingError()) 
       return false;
   string PkgName = Pkg.Name();
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
	  RPMTAG_VERSION,
	  RPMTAG_RELEASE,
	  RPMTAG_ARCH,
	  RPMTAG_REQUIRENAME,
	  RPMTAG_OBSOLETENAME,
	  RPMTAG_CONFLICTNAME,
	  0
   };
   unsigned long Result = INIT_FCS;
   
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
bool rpmListParser::ParseStatus(pkgCache::PkgIterator &Pkg,
				pkgCache::VerIterator &Ver)
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


bool rpmListParser::ParseDepends(pkgCache::VerIterator &Ver,
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
#if RPM_VERSION >= 0x040403
	 else if (flagl[i] & RPMSENSE_MISSINGOK)
	    Type = pkgCache::Dep::Suggests;
#endif
	 else
	    Type = pkgCache::Dep::Depends;
      }

#if RPM_VERSION >= 0x040404 && 0
      if (namel[i][0] == 'g' && strncmp(namel[i], "getconf", 7) == 0)
      {
        rpmds getconfProv = NULL;
        rpmds ds = rpmdsSingle(RPMTAG_PROVIDENAME,
                               namel[i], verl?verl[i]:NULL, flagl[i]);
        rpmdsGetconf(&getconfProv, NULL);
        int res = rpmdsSearch(getconfProv, ds) >= 0;
        rpmdsFree(ds);
        rpmdsFree(getconfProv);
        if (res) continue;
      }
#endif
      
      if (namel[i][0] == 'r' && strncmp(namel[i], "rpmlib", 6) == 0)
      {
#if HAVE_RPMCHECKRPMLIBPROVIDES && RPM_VERSION >= 0x040d00 /* 4.13.0 (ALT specific) */
	 int res = rpmCheckRpmlibProvides(namel[i], verl?verl[i]:NULL,
					  flagl[i]);
#elif RPM_VERSION >= 0x040404
        rpmds rpmlibProv = NULL;
        rpmds ds = rpmdsSingle(RPMTAG_PROVIDENAME,
                               namel[i], verl?verl[i]:NULL, flagl[i]);
        rpmdsRpmlib(&rpmlibProv, NULL);
        int res = rpmdsSearch(rpmlibProv, ds) >= 0;
        rpmdsFree(ds);
        rpmdsFree(rpmlibProv);
#elif RPM_VERSION >= 0x040100
	 rpmds ds = rpmdsSingle(RPMTAG_PROVIDENAME,
			        namel[i], verl?verl[i]:NULL, flagl[i]);
	 int res = rpmCheckRpmlibProvides(ds);
	 rpmdsFree(ds);
#else
	 int res = rpmCheckRpmlibProvides(namel[i], verl?verl[i]:NULL,
					  flagl[i]);
#endif
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
bool rpmListParser::ParseDepends(pkgCache::VerIterator &Ver,
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
#if RPM_VERSION >= 0x040403 && 0
   case pkgCache::Dep::Suggests:
      res = headerGetEntry(header, RPMTAG_SUGGESTSNAME, &type, 
			   (void **)&namel, &count);
      if (res != 1)
	  return true;
      res = headerGetEntry(header, RPMTAG_SUGGESTSVERSION, &type, 
			   (void **)&verl, &count);
      res = headerGetEntry(header, RPMTAG_SUGGESTSFLAGS, &type,
			   (void **)&flagl, &count);
      break;
#if 0 // Enhances is not even known to apt, sigh...
   case pkgCache::Dep::Enhances:
      res = headerGetEntry(header, RPMTAG_ENHANCESNAME, &type, 
			   (void **)&namel, &count);
      if (res != 1)
	  return true;
      res = headerGetEntry(header, RPMTAG_ENHANCESVERSION, &type, 
			   (void **)&verl, &count);
      res = headerGetEntry(header, RPMTAG_ENHANCESFLAGS, &type,
			   (void **)&flagl, &count);
      break;
#endif
#endif
   }
   
   ParseDepends(Ver, namel, verl, flagl, count, Type);

   free(namel);
   free(verl);
   
   return true;
}
                                                                        /*}}}*/
#ifdef OLD_FILEDEPS
bool rpmListParser::ProcessFileProvides(pkgCache::VerIterator Ver)
{
   const char **names = NULL;    
   rpm_count_t count = 0;

   rpmHeaderGetEntry(header, RPMTAG_OLDFILENAMES, NULL, &names, &count);

   while (count--) 
   {
      if (rpmSys.IsFileDep(string(names[count]))) 
      {
	 if (!NewProvides(Ver, string(names[count]), string()))
	     return false;
      }
   }

   return true;
}
#endif

bool rpmListParser::CollectFileProvides(pkgCache &Cache,
					pkgCache::VerIterator &Ver)
{
   bool ret = true;
   const char *FileName;
   int count = 0;
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
bool rpmListParser::ParseProvides(pkgCache::VerIterator &Ver)
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
	  RpmData->ArchScore(Architecture().c_str()) > 0)
	 return true;
   }
   header = NULL;
   return false;
}
                                                                        /*}}}*/
// ListParser::LoadReleaseInfo - Load the release information		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool rpmListParser::LoadReleaseInfo(pkgCache::PkgFileIterator &FileI,
				    FileFd &File)
{
   pkgTagFile Tags(&File);
   pkgTagSection Section;
   if (!Tags.Step(Section))
       return false;
   
   const char *Start;
   const char *Stop;

   if (Section.Find("Archive",Start,Stop))
   {
      const auto idx = WriteUniqString(Start, Stop - Start);
      if (!idx)
         return false;

       FileI->Archive = *idx;
   }

   if (Section.Find("Component",Start,Stop))
   {
      const auto idx = WriteUniqString(Start, Stop - Start);
      if (!idx)
         return false;

      FileI->Component = *idx;
   }

   if (Section.Find("Version",Start,Stop))
   {
      const auto idx = WriteUniqString(Start, Stop - Start);
      if (!idx)
         return false;

      FileI->Version = *idx;
   }

   if (Section.Find("Origin",Start,Stop))
   {
      const auto idx = WriteUniqString(Start, Stop - Start);
      if (!idx)
         return false;

      FileI->Origin = *idx;
   }

   if (Section.Find("Label",Start,Stop))
   {
      const auto idx = WriteUniqString(Start, Stop - Start);
      if (!idx)
         return false;

      FileI->Label = *idx;
   }

   if (Section.Find("Architecture",Start,Stop))
   {
      const auto idx = WriteUniqString(Start, Stop - Start);
      if (!idx)
         return false;

      FileI->Architecture = *idx;
   }

   if (Section.FindFlag("NotAutomatic",FileI->Flags,
			pkgCache::Flag::NotAutomatic) == false)
       _error->Warning(_("Bad NotAutomatic flag"));
   
   return !_error->PendingError();
}
                                                                        /*}}}*/

unsigned long long rpmListParser::Size()
{
   uint32_t *size;
   rpm_tagtype_t type;
   rpm_count_t count;
   if (headerGetEntry(header, RPMTAG_SIZE, &type, (void **)&size, &count)!=1)
       return 1;
   return (size[0]+512)/1024;
}

unsigned long rpmListParser::Flags()
{
   unsigned long returnflags = 0;
   
   uint32_t *autoinstalled;
   rpm_tagtype_t type;
   rpm_count_t count;

   if ((headerGetEntry(header, RPMTAG_AUTOINSTALLED, &type, (void**)&autoinstalled, &count) == 1)
      && (type == RPM_INT32_TYPE)
      && (count == 1))
   {
      if (*autoinstalled)
      {
         returnflags |= pkgCache::Flag::Auto;
      }
   }

   return returnflags;
}

// This is a slightly complex operation. It must take a package, and
// move every version to new packages, named accordingly to
// Allow-Duplicated rules.
void rpmListParser::VirtualizePackage(const string &Name)
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
