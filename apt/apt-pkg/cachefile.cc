// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: cachefile.cc,v 1.2 2002/07/25 18:07:18 niemeyer Exp $
/* ######################################################################
   
   CacheFile - Simple wrapper class for opening, generating and whatnot
   
   This class implements a simple 2 line mechanism to open various sorts
   of caches. It can operate as root, as not root, show progress and so on,
   it transparently handles everything necessary.
   
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#ifdef __GNUG__
#pragma implementation "apt-pkg/cachefile.h"
#endif

#include <config.h>

#include <algorithm>
#include <sstream>
#include <string.h>
#include <unistd.h>

#include <apt-pkg/cachefile.h>
#include <apt-pkg/error.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/pkgcachegen.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/policy.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/version.h>
    
#include <apti18n.h>
									/*}}}*/

using namespace std;

// CacheFile::CacheFile - Constructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgCacheFile::pkgCacheFile() : Map(0), Cache(0), DCache(0), Policy(0)
{
}
									/*}}}*/
// CacheFile::~CacheFile - Destructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgCacheFile::~pkgCacheFile()
{
   delete DCache;
   delete Policy;
   delete Cache;
   delete Map;
   _system->UnLock(true);
}   
									/*}}}*/
// CacheFile::BuildCaches - Open and build the cache files		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool pkgCacheFile::BuildCaches(OpProgress &Progress,bool WithLock)
{
   if (WithLock == true)
      if (_system->Lock() == false)
	 return false;

   // CNC:2002-07-06
   if (WithLock == false)
      _system->LockRead();
   
   if (_config->FindB("Debug::NoLocking",false) == true)
      WithLock = false;
      
   if (_error->PendingError() == true)
      return false;
   
   // Read the source list
   pkgSourceList List;
   if (List.ReadMainList() == false)
      return _error->Error(_("The list of sources could not be read."));

   // Read the caches
   bool Res = pkgMakeStatusCache(List,Progress,&Map,!WithLock);
   Progress.Done();
   if (Res == false)
      return _error->Error(_("The package lists or status file could not be parsed or opened."));

   /* This sux, remove it someday */
   if (_error->empty() == false)
      _error->Warning(_("You may want to run apt-get update to correct these problems"));

   Cache = new pkgCache(Map);
   if (_error->PendingError() == true)
      return false;
   return true;
}
									/*}}}*/
// CacheFile::Open - Open the cache files, creating if necessary	/*{{{*/
// ---------------------------------------------------------------------
/* */
bool pkgCacheFile::Open(OpProgress &Progress,bool WithLock)
{
   if (BuildCaches(Progress,WithLock) == false)
      return false;
   
   // The policy engine
   Policy = new pkgPolicy(Cache);
   if (_error->PendingError() == true)
      return false;
   if (ReadPinFile(*Policy) == false || ReadPinDir(*Policy) == false)
      return false;
   
   // Create the dependency cache
   DCache = new pkgDepCache(Cache,Policy);
   if (_error->PendingError() == true)
      return false;
   
   DCache->Init(&Progress);
   Progress.Done();
   if (_error->PendingError() == true)
      return false;
   
   return true;
}
									/*}}}*/

// CacheFile::Close - close the cache files				/*{{{*/
// ---------------------------------------------------------------------
/* */
void pkgCacheFile::Close()
{
   delete DCache;
   delete Policy;
   delete Cache;
   delete Map;
   _system->UnLock(true);

   Map = 0;
   DCache = 0;
   Policy = 0;
   Cache = 0;
}
									/*}}}*/

pkgCache *CacheFile::SortCache = 0;

CacheFile::CacheFile(std::ostream &c1out)
   : m_c1out(c1out),
   m_is_root((geteuid() == 0))
{
   List = 0;
}
int CacheFile::NameComp(const void *a,const void *b)
{
   if (*(pkgCache::Package **)a == 0 || *(pkgCache::Package **)b == 0)
      return *(pkgCache::Package **)a - *(pkgCache::Package **)b;
   
   const pkgCache::Package &A = **(pkgCache::Package **)a;
   const pkgCache::Package &B = **(pkgCache::Package **)b;

   return strcmp(SortCache->StrP + A.Name,SortCache->StrP + B.Name);
}
									/*}}}*/
// CacheFile::Sort - Sort by name					/*{{{*/
// ---------------------------------------------------------------------
/* */
void CacheFile::Sort()
{
   delete [] List;
   List = new pkgCache::Package *[Cache->Head().PackageCount];
   memset(List,0,sizeof(*List)*Cache->Head().PackageCount);
   pkgCache::PkgIterator I = Cache->PkgBegin();
   for (;I.end() != true; I++)
      List[I->ID] = I;

   SortCache = *this;
   qsort(List,Cache->Head().PackageCount,sizeof(*List),NameComp);
}
									/*}}}*/
// CacheFile::CheckDeps - Open the cache file				/*{{{*/
// ---------------------------------------------------------------------
/* This routine generates the caches and then opens the dependency cache
   and verifies that the system is OK. */
bool CacheFile::CheckDeps(bool AllowBroken)
{
   if (_error->PendingError() == true)
      return false;

// CNC:2003-03-19 - Might be changed by some extension.
#if 0
   // Check that the system is OK
   if (DCache->DelCount() != 0 || DCache->InstCount() != 0)
      return _error->Error("Internal Error, non-zero counts");
#endif
   
   // Apply corrections for half-installed packages
   if (pkgApplyStatus(*DCache) == false)
      return false;
   
   // Nothing is broken
   if (DCache->BrokenCount() == 0 || AllowBroken == true)
      return true;

   // Attempt to fix broken things
   if (_config->FindB("APT::Get::Fix-Broken",false) == true)
   {
      m_c1out << _("Correcting dependencies...") << flush;
      if (pkgFixBroken(*DCache) == false || DCache->BrokenCount() != 0)
      {
	 m_c1out << _(" failed.") << endl;
	 ShowBroken(m_c1out,*this,true);

	 return _error->Error(_("Unable to correct dependencies"));
      }
      if (pkgMinimizeUpgrade(*DCache) == false)
	 return _error->Error(_("Unable to minimize the upgrade set"));
      
      m_c1out << _(" Done") << endl;
   }
   else
   {
      m_c1out << _("You might want to run `apt-get --fix-broken install' to correct these.") << endl;
      ShowBroken(m_c1out,*this,true);

      return _error->Error(_("Unmet dependencies. Try using --fix-broken."));
   }
      
   return true;
}

bool CacheFile::BuildCaches(bool WithLock)
{
   OpTextProgress Prog(*_config);
   if (pkgCacheFile::BuildCaches(Prog,WithLock) == false)
  return false;
   return true;
}

bool CacheFile::Open(bool WithLock) 
{
   OpTextProgress Prog(*_config);
   if (pkgCacheFile::Open(Prog,WithLock) == false)
  return false;
   Sort();
   
   return true;
};

bool CacheFile::OpenForInstall()
{
   // CNC:2004-03-07 - dont take lock if in download mode
   if (_config->FindB("APT::Get::Print-URIs") == true ||
   _config->FindB("APT::Get::Download-only") == true)
  return Open(false);
   else
  return Open(true);
}

bool CacheFile::CanCommit() const
{
   return m_is_root;
}

// ShowList - Show a list						/*{{{*/
// ---------------------------------------------------------------------
/* This prints out a string of space separated words with a title and 
   a two space indent line wraped to the current screen width. */
bool ShowList(std::ostream &out, const std::string &Title, std::string List, const std::string &VersionsList, size_t l_ScreenWidth)
{
   if (List.empty() == true)
      return true;
   // trim trailing space
   int NonSpace = List.find_last_not_of(' ');
   if (NonSpace != -1)
   {
      List = List.erase(NonSpace + 1);
      if (List.empty() == true)
	 return true;
   }

   // Acount for the leading space
   l_ScreenWidth = l_ScreenWidth - 3;
      
   out << Title << endl;
   string::size_type Start = 0;
   string::size_type VersionsStart = 0;
   while (Start < List.size())
   {
      if(_config->FindB("APT::Get::Show-Versions",false) == true &&
         VersionsList.size() > 0) {
         string::size_type End;
         string::size_type VersionsEnd;
         
         End = List.find(' ',Start);
         VersionsEnd = VersionsList.find('\n', VersionsStart);

         out << "   " << string(List,Start,End - Start) << " (" << 
            string(VersionsList,VersionsStart,VersionsEnd - VersionsStart) << 
            ")" << endl;

	 if (End == string::npos || End < Start)
	    End = Start + l_ScreenWidth;

         Start = End + 1;
         VersionsStart = VersionsEnd + 1;
      } else {
         string::size_type End;

         if (Start + l_ScreenWidth >= List.size())
            End = List.size();
         else
            End = List.rfind(' ',Start+l_ScreenWidth);

         if (End == string::npos || End < Start)
            End = Start + l_ScreenWidth;
         out << "  " << string(List,Start,End - Start) << endl;
         Start = End + 1;
      }
   }   

   return false;
}
									/*}}}*/
// ShowBroken - Debugging aide						/*{{{*/
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
void ShowBroken(ostream &out,CacheFile &Cache,bool Now)
{
   out << _("The following packages have unmet dependencies:") << endl;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      
      if (Now == true)
      {
	 if (Cache[I].NowBroken() == false)
	    continue;
      }
      else
      {
	 if (Cache[I].InstBroken() == false)
	    continue;
      }
      
      // Print out each package and the failed dependencies
      out <<"  " <<  I.Name() << ":";
      unsigned Indent = strlen(I.Name()) + 3;
      bool First = true;
      pkgCache::VerIterator Ver;
      
      if (Now == true)
	 Ver = I.CurrentVer();
      else
	 Ver = Cache[I].InstVerIter(Cache);
      
      if (Ver.end() == true)
      {
	 out << endl;
	 continue;
      }
      
      for (pkgCache::DepIterator D = Ver.DependsList(); D.end() == false;)
      {
	 // Compute a single dependency element (glob or)
	 pkgCache::DepIterator Start;
	 pkgCache::DepIterator End;
	 D.GlobOr(Start,End);

         // CNC:2003-02-22 - IsImportantDep() currently calls IsCritical(), so
         //		     these two are currently doing the same thing. Check
         //		     comments in IsImportantDep() definition.
#if 0
	 if (Cache->IsImportantDep(End) == false)
	    continue;
#else
	 if (End.IsCritical() == false)
	    continue;
#endif
	 
	 if (Now == true)
	 {
	    if ((Cache[End] & pkgDepCache::DepGNow) == pkgDepCache::DepGNow)
	       continue;
	 }
	 else
	 {
	    if ((Cache[End] & pkgDepCache::DepGInstall) == pkgDepCache::DepGInstall)
	       continue;
	 }
	 
	 bool FirstOr = true;
	 while (1)
	 {
	    if (First == false)
	       for (unsigned J = 0; J != Indent; J++)
		  out << ' ';
	    First = false;

	    if (FirstOr == false)
	    {
	       for (unsigned J = 0; J != strlen(End.DepType()) + 3; J++)
		  out << ' ';
	    }
	    else
	       out << ' ' << End.DepType() << ": ";
	    FirstOr = false;
	    
	    out << Start.TargetPkg().Name();
	 
	    // Show a quick summary of the version requirements
	    if (Start.TargetVer() != 0)
	       out << " (" << Start.CompType() << " " << Start.TargetVer() << ")";
	    
	    /* Show a summary of the target package if possible. In the case
	       of virtual packages we show nothing */	 
	    pkgCache::PkgIterator Targ = Start.TargetPkg();
	    if (Targ->ProvidesList == 0)
	    {
	       out << ' ';
	       pkgCache::VerIterator Ver = Cache[Targ].InstVerIter(Cache);
	       if (Now == true)
		  Ver = Targ.CurrentVer();
	       	    
	       if (Ver.end() == false)
	       {
		  if (Now == true)
		     ioprintf(out,_("but %s is installed"),Ver.VerStr());
		  else
		     ioprintf(out,_("but %s is to be installed"),Ver.VerStr());
	       }	       
	       else
	       {
		  if (Cache[Targ].CandidateVerIter(Cache).end() == true)
		  {
		     if (Targ->ProvidesList == 0)
			out << _("but it is not installable");
		     else
			out << _("but it is a virtual package");
		  }		  
		  else
		     out << (Now?_("but it is not installed"):_("but it is not going to be installed"));
	       }	       
	    }
	    
	    if (Start != End)
	       out << _(" or");
	    out << endl;
	    
	    if (Start == End)
	       break;
	    Start++;
	 }	 
      }	    
   }   
}
									/*}}}*/
// ShowNew - Show packages to newly install				/*{{{*/
// ---------------------------------------------------------------------
/* */
void ShowNew(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth)
{
   /* Print out a list of packages that are going to be installed extra
      to what the user asked */
   string List;
   string VersionsList;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      if (Cache[I].NewInstall() == true &&
	  (State == NULL || (*State)[I].NewInstall() == false)) {
	 List += string(I.Name()) + " ";
         VersionsList += string(Cache[I].CandVersion) + "\n";
      }
   }
   
   if (!List.empty()) l_c3out<<"apt-get:install-list:"<<List<<endl;
   ShowList(out,_("The following NEW packages will be installed:"),List,VersionsList,l_ScreenWidth);
}
									/*}}}*/
// ShowDel - Show packages to delete					/*{{{*/
// ---------------------------------------------------------------------
/* */
void ShowDel(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth)
{
   /* Print out a list of packages that are going to be removed extra
      to what the user asked */
   string List, RepList; // CNC:2002-07-25
   string VersionsList;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      if (Cache[I].Delete() == true &&
	  (State == NULL || (*State)[I].Delete() == false))
      {
	 // CNC:2002-07-25
	 bool Obsoleted = false;
	 string by;
	 for (pkgCache::DepIterator D = I.RevDependsList(); D.end() == false; D++)
	 {
	    if (D->Type == pkgCache::Dep::Obsoletes &&
	        Cache[D.ParentPkg()].Install() &&
	        (pkgCache::Version*)D.ParentVer() == Cache[D.ParentPkg()].InstallVer &&
	        Cache->VS().CheckDep(I.CurrentVer().VerStr(), D) == true)
	    {
	       if (Obsoleted)
		  by += ", " + string(D.ParentPkg().Name());
	       else
	       {
		  Obsoleted = true;
		  by = D.ParentPkg().Name();
	       }
	    }
	 }
	 if (Obsoleted)
	    RepList += string(I.Name()) + " (by " + by + ")  ";
	 else
	 {
	    if ((Cache[I].iFlags & pkgDepCache::Purge) == pkgDepCache::Purge)
	       List += string(I.Name()) + "* ";
	    else
	       List += string(I.Name()) + " ";
	 }
     
     // CNC:2004-03-09
     VersionsList += string(I.CurrentVer().VerStr())+ "\n";
      }
   }
   
   // CNC:2002-07-25
   if (!RepList.empty()) l_c3out<<"apt-get:replace-list:"<<RepList<<endl;;
   ShowList(out,_("The following packages will be REPLACED:"),RepList,VersionsList,l_ScreenWidth);
   if (!List.empty()) l_c3out<<"apt-get:remove-list:"<<List<<endl;
   ShowList(out,_("The following packages will be REMOVED:"),List,VersionsList,l_ScreenWidth);
}
									/*}}}*/
// ShowKept - Show kept packages					/*{{{*/
// ---------------------------------------------------------------------
/* */
void ShowKept(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth)
{
   string List;
   string VersionsList;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {	 
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      
      if (State == NULL) {
	 // Not interesting
	 if (Cache[I].Upgrade() == true || Cache[I].Upgradable() == false ||
	     I->CurrentVer == 0 || Cache[I].Delete() == true)
	    continue;
      } else {
	 // Not interesting
	 if (!((Cache[I].Install() == false && (*State)[I].Install() == true) ||
	       (Cache[I].Delete() == false && (*State)[I].Delete() == true)))
	    continue;
      }
      
      List += string(I.Name()) + " ";
      VersionsList += string(Cache[I].CurVersion) + " => " + Cache[I].CandVersion + "\n";
   }
   if (!List.empty()) l_c3out<<"apt-get:keep-list:"<<List<<endl;
   ShowList(out,_("The following packages have been kept back"),List,VersionsList,l_ScreenWidth);
}
									/*}}}*/
// ShowUpgraded - Show upgraded packages				/*{{{*/
// ---------------------------------------------------------------------
/* */
void ShowUpgraded(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth)
{
   string List;
   string VersionsList;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      
      if (State == NULL) {
	 // Not interesting
	 if (Cache[I].Upgrade() == false || Cache[I].NewInstall() == true)
	    continue;
      } else {
	 // Not interesting
	 if (Cache[I].NewInstall() == true ||
	     !(Cache[I].Upgrade() == true && (*State)[I].Upgrade() == false))
	    continue;
      }
      
      List += string(I.Name()) + " ";
      VersionsList += string(Cache[I].CurVersion) + " => " + Cache[I].CandVersion + "\n";
   }
   if (!List.empty()) l_c3out<<"apt-get:upgrade-list:"<<List<<endl;
   ShowList(out,_("The following packages will be upgraded"),List,VersionsList,l_ScreenWidth);
}
									/*}}}*/
// ShowDowngraded - Show downgraded packages				/*{{{*/
// ---------------------------------------------------------------------
/* */
bool ShowDowngraded(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth)
{
   string List;
   string VersionsList;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      
      if (State == NULL) {
	 // Not interesting
	 if (Cache[I].Downgrade() == false || Cache[I].NewInstall() == true)
	    continue;
      } else {
	 // Not interesting
	 if (Cache[I].NewInstall() == true ||
	     !(Cache[I].Downgrade() == true && (*State)[I].Downgrade() == false))
	    continue;
      }
      
      List += string(I.Name()) + " ";
      VersionsList += string(Cache[I].CurVersion) + " => " + Cache[I].CandVersion + "\n";
   }
   if (!List.empty()) l_c3out<<"apt-get:downgrade-list:"<<List<<endl;
   return ShowList(out,_("The following packages will be DOWNGRADED"),List,VersionsList,l_ScreenWidth);
}
									/*}}}*/
// ShowHold - Show held but changed packages				/*{{{*/
// ---------------------------------------------------------------------
/* */
bool ShowHold(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth)
{
   string List;
   string VersionsList;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      if (Cache[I].InstallVer != (pkgCache::Version *)I.CurrentVer() &&
	  I->SelectedState == pkgCache::State::Hold &&
	  (State == NULL ||
	   Cache[I].InstallVer != (*State)[I].InstallVer)) {
	 List += string(I.Name()) + " ";
	 VersionsList += string(Cache[I].CurVersion) + " => " + Cache[I].CandVersion + "\n";
      }
   }
   
   if (!List.empty()) l_c3out<<"apt-get:hold-list:"<<List<<endl;
   return ShowList(out,_("The following held packages will be changed:"),List,VersionsList,l_ScreenWidth);
}
									/*}}}*/
// ShowEssential - Show an essential package warning			/*{{{*/
// ---------------------------------------------------------------------
/* This prints out a warning message that is not to be ignored. It shows
   all essential packages and their dependents that are to be removed. 
   It is insanely risky to remove the dependents of an essential package! */
bool ShowEssential(std::ostream &out, std::ostream &l_c3out, CacheFile &Cache, pkgDepCache::State *State, size_t l_ScreenWidth)
{
   string List;
   string VersionsList;
   bool *Added = new bool[Cache->Head().PackageCount];
   for (unsigned int I = 0; I != Cache->Head().PackageCount; I++)
      Added[I] = false;
   
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      if ((I->Flags & pkgCache::Flag::Essential) != pkgCache::Flag::Essential &&
	  (I->Flags & pkgCache::Flag::Important) != pkgCache::Flag::Important)
	 continue;
      
      // The essential package is being removed
      if (Cache[I].Delete() == true &&
	  (State == NULL || (*State)[I].Delete() == false))
      {
	 if (Added[I->ID] == false)
	 {
	    // CNC:2003-03-21 - Do not consider a problem if that package is being obsoleted
	    //                  by something else.
	    bool Obsoleted = false;
	    for (pkgCache::DepIterator D = I.RevDependsList(); D.end() == false; D++)
	    {
	       if (D->Type == pkgCache::Dep::Obsoletes &&
		   Cache[D.ParentPkg()].Install() &&
		   ((pkgCache::Version*)D.ParentVer() == Cache[D.ParentPkg()].InstallVer ||
		    (pkgCache::Version*)D.ParentVer() == ((pkgCache::Version*)D.ParentPkg().CurrentVer())) &&
		   Cache->VS().CheckDep(I.CurrentVer().VerStr(), D) == true)
	       {
		  Obsoleted = true;
		  break;
	       }
	    }
	    if (Obsoleted == false) {
	       Added[I->ID] = true;
	       List += string(I.Name()) + " ";
	    }
        //VersionsList += string(Cache[I].CurVersion) + "\n"; ???
	 }
      }
      
      if (I->CurrentVer == 0)
	 continue;

      // Print out any essential package depenendents that are to be removed
      for (pkgCache::DepIterator D = I.CurrentVer().DependsList(); D.end() == false; D++)
      {
	 // Skip everything but depends
	 if (D->Type != pkgCache::Dep::PreDepends &&
	     D->Type != pkgCache::Dep::Depends)
	    continue;
	 
	 pkgCache::PkgIterator P = D.SmartTargetPkg();
	 if (Cache[P].Delete() == true &&
	     (State == NULL || (*State)[P].Delete() == false))
	 {
	    if (Added[P->ID] == true)
	       continue;

	    // CNC:2003-03-21 - Do not consider a problem if that package is being obsoleted
	    //                  by something else.
	    bool Obsoleted = false;
	    for (pkgCache::DepIterator D = P.RevDependsList(); D.end() == false; D++)
	    {
	       if (D->Type == pkgCache::Dep::Obsoletes &&
		   Cache[D.ParentPkg()].Install() &&
		   ((pkgCache::Version*)D.ParentVer() == Cache[D.ParentPkg()].InstallVer ||
		    (pkgCache::Version*)D.ParentVer() == ((pkgCache::Version*)D.ParentPkg().CurrentVer())) &&
		   Cache->VS().CheckDep(P.CurrentVer().VerStr(), D) == true)
	       {
		  Obsoleted = true;
		  break;
	       }
	    }
	    if (Obsoleted == true)
	       continue;

	    Added[P->ID] = true;
	    
	    char S[300];
	    snprintf(S,sizeof(S),_("%s (due to %s) "),P.Name(),I.Name());
	    List += S;
        //VersionsList += "\n"; ???
	 }	 
      }      
   }
   
   delete [] Added;
   if (!List.empty()) l_c3out<<"apt-get:essential-list:"<<List<<endl;
   return ShowList(out,_("WARNING: The following essential packages will be removed\n"
			 "This should NOT be done unless you know exactly what you are doing!"),List,VersionsList,l_ScreenWidth);
}
									/*}}}*/
// Stats - Show some statistics						/*{{{*/
// ---------------------------------------------------------------------
/* */
void Stats(std::ostream &out, std::ostream &l_c3out, pkgDepCache &Dep, pkgDepCache::State *State)
{
   unsigned long Upgrade = 0;
   unsigned long Downgrade = 0;
   unsigned long Install = 0;
   unsigned long ReInstall = 0;
   // CNC:2002-07-29
   unsigned long Replace = 0;
   unsigned long Remove = 0;
   unsigned long Keep = 0;
   for (pkgCache::PkgIterator I = Dep.PkgBegin(); I.end() == false; I++)
   {
      if (Dep[I].NewInstall() == true &&
	  (State == NULL || (*State)[I].NewInstall() == false))
	 Install++;
      else
      {
	 if (Dep[I].Upgrade() == true &&
	     (State == NULL || (*State)[I].Upgrade() == false))
	    Upgrade++;
	 else
	    if (Dep[I].Downgrade() == true &&
		(State == NULL || (*State)[I].Downgrade() == false))
	       Downgrade++;
	    else
	       if (State != NULL &&
		   (((*State)[I].NewInstall() == true && Dep[I].NewInstall() == false) ||
		    ((*State)[I].Upgrade() == true && Dep[I].Upgrade() == false) ||
		    ((*State)[I].Downgrade() == true && Dep[I].Downgrade() == false)))
		  Keep++;
      }
      // CNC:2002-07-29
      if (Dep[I].Delete() == true &&
	  (State == NULL || (*State)[I].Delete() == false))
      {
	 bool Obsoleted = false;
	 string by;
	 for (pkgCache::DepIterator D = I.RevDependsList();
	      D.end() == false; D++)
	 {
	    if (D->Type == pkgCache::Dep::Obsoletes &&
	        Dep[D.ParentPkg()].Install() &&
	        (pkgCache::Version*)D.ParentVer() == Dep[D.ParentPkg()].InstallVer &&
	        Dep.VS().CheckDep(I.CurrentVer().VerStr(), D) == true)
	    {
	       Obsoleted = true;
	       break;
	    }
	 }
	 if (Obsoleted)
	    Replace++;
	 else
	    Remove++;
      }
      else if ((Dep[I].iFlags & pkgDepCache::ReInstall) == pkgDepCache::ReInstall &&
	       (State == NULL || !((*State)[I].iFlags & pkgDepCache::ReInstall)))
	 ReInstall++;
   }   
   l_c3out<<"apt-get:status:upgrade:"<<Upgrade<<endl;
   l_c3out<<"apt-get:status:downgrade:"<<Downgrade<<endl;
   l_c3out<<"apt-get:status:install:"<<Install<<endl;
   l_c3out<<"apt-get:status:re-install:"<<ReInstall<<endl;
   l_c3out<<"apt-get:status:replace:"<<Replace<<endl;
   l_c3out<<"apt-get:status:remove:"<<Remove<<endl;

   ioprintf(out,_("%lu upgraded, %lu newly installed, "),
	    Upgrade,Install);
   
   if (ReInstall != 0)
      ioprintf(out,_("%lu reinstalled, "),ReInstall);
   if (Downgrade != 0)
      ioprintf(out,_("%lu downgraded, "),Downgrade);
   // CNC:2002-07-29
   if (Replace != 0)
      ioprintf(out,_("%lu replaced, "),Replace);

   // CNC:2002-07-29
   if (State == NULL)
      ioprintf(out,_("%lu removed and %lu not upgraded.\n"),
	       Remove,Dep.KeepCount());
   else
      ioprintf(out,_("%lu removed and %lu kept.\n"),Remove,Keep);

   
   if (Dep.BadCount() != 0)
      ioprintf(out,_("%lu not fully installed or removed.\n"),
	       Dep.BadCount());
}
