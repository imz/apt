// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: apt-get.cc,v 1.45 2001/11/13 18:00:07 kojima Exp $
/* ######################################################################
   
   apt-get - Cover for dpkg and rpm
   
   This is an allout cover for dpkg implementing a safer front end. It is
   based largely on libapt-pkg.

   The syntax is different, 
      apt-get [opt] command [things]
   Where command is:
      update - Resyncronize the package files from their sources
      upgrade - Smart-Download the newest versions of all packages
      dselect-upgrade - Follows dselect's changes to the Status: field
                       and installes new and removes old packages
      dist-upgrade - Powerfull upgrader designed to handle the issues with
                    a new distribution.
      install - Download and install a given package (by name, not by .deb)
      check - Update the package cache and check for broken packages
      clean - Erase the .debs downloaded to /var/cache/apt/archives and
              the partial dir too

   ##################################################################### 
 */

//#define DEBUG
									/*}}}*/
// Include Files							/*{{{*/
#include <apt-pkg/error.h>
#include <apt-pkg/cmndline.h>
#include <apt-pkg/init.h>
#include <apt-pkg/depcache.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/acquire-item.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/clean.h>
#include <apt-pkg/srcrecords.h>
#include <apt-pkg/cachefile.h>

#include <apt-pkg/debfactory.h>
#include <apt-pkg/rpmfactory.h>


#ifdef DEBUG
#include <malloc.h>
#endif

#include <config.h>

#include <i18n.h>

#include "acqprogress.h"

#include <fstream.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <regex.h>
#include <sys/wait.h>
									/*}}}*/


ostream c0out;
ostream c1out;
ostream c2out;
ofstream devnull("/dev/null");
unsigned int ScreenWidth = 80;

// class CacheFile - Cover class for some dependency cache functions	/*{{{*/
// ---------------------------------------------------------------------
/* */
class CacheFile : public pkgCacheFile
{
   static pkgCache *SortCache;
   static int NameComp(const void *a,const void *b);
   
   public:
   pkgCache::Package **List;
   
   void Sort();
   bool CheckDeps(bool AllowBroken = false);
   bool Open(bool WithLock = true) 
   {
      OpTextProgress Prog(*_config);
       
      if (pkgCacheFile::Open(Prog,WithLock) == false)
	 return false;
      Sort();
      return true;
   };

#if 1
   Header header;
#endif
   bool LoadRecord(pkgCache::VerIterator V);
   const char *GetPriority();
   const char *GetSummary();
   const char *GetDate();
   
   CacheFile() : List(0), header(0) {};
};
									/*}}}*/

// YnPrompt - Yes No Prompt.						/*{{{*/
// ---------------------------------------------------------------------
/* Returns true on a Yes.*/
bool YnPrompt()
{
   if (_config->FindB("APT::Get::Assume-Yes",false) == true)
   {
      c1out << 'Y' << endl;
      return true;
   }
   
   char C = 0;
   char Jnk = 0;
   read(STDIN_FILENO,&C,1);
   while (C != '\n' && Jnk != '\n') read(STDIN_FILENO,&Jnk,1);
   
   /* Yes/No */
   if (!(C == *_("Y") || C == *_("y") || C == '\n' || C == '\r'))
      return false;
   return true;
}
									/*}}}*/
// AnalPrompt - Annoying Yes No Prompt.					/*{{{*/
// ---------------------------------------------------------------------
/* Returns true on a Yes.*/
bool AnalPrompt(const char *Text)
{
   char Buf[1024];
   cin.getline(Buf,sizeof(Buf));
   if (strcmp(Buf,Text) == 0)
      return true;
   return false;
}
									/*}}}*/
// ShowList - Show a list						/*{{{*/
// ---------------------------------------------------------------------
/* This prints out a string of space seperated words with a title and 
   a two space indent line wraped to the current screen width. */
bool ShowList(ostream &out,string Title,string List)
{
   if (List.empty() == true)
      return true;

   // Acount for the leading space
   int ScreenWidth = ::ScreenWidth - 3;
      
   out << Title << endl;
   string::size_type Start = 0;
   while (Start < List.size())
   {
      string::size_type End;
      if (Start + ScreenWidth >= List.size())
	 End = List.size();
      else
	 End = List.rfind(' ',Start+ScreenWidth);

      if (End == string::npos || End < Start)
	 End = Start + ScreenWidth;
      out << "  " << string(List,Start,End - Start) << endl;
      Start = End + 1;
   }   
   return false;
}
									/*}}}*/


void ShowText(ostream &out, const char *text)
{
   const char *begin = text;
   const char *end = text;

   while (*begin)
   {
      const char *p;

      p = begin;
      while (1)
      {
	 p++;
	 if (*p == '\n') 
	 {
	    end = p+1;
	    break;
	 }
	 else if (*p == '\0') 
	 {
	    end = p;
	    break;
	 }
      }
      out << " " << string(begin, end);
      begin = end;
   }
   out << endl;
}

// ShowBroken - Debugging aide						/*{{{*/
// ---------------------------------------------------------------------
/* This prints out the names of all the packages that are broken along
   with the name of each each broken dependency and a quite version 
   description. */
void ShowBroken(ostream &out,CacheFile &Cache,bool Now)
{
   out << _("Sorry, but the following packages have unmet dependencies:") << endl;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
       
      if (Cache[I].InstBroken() == false)
	  continue;
	  
      // Print out each package and the failed dependencies
      out <<"  " <<  I.Name() << ":";
      unsigned Indent = strlen(I.Name()) + 3;
      bool First = true;
      if (Cache[I].InstVerIter(Cache).end() == true)
      {
	 cout << endl;
	 continue;
      }
      
      for (pkgCache::DepIterator D = Cache[I].InstVerIter(Cache).DependsList(); D.end() == false;)
      {
	 // Compute a single dependency element (glob or)
	 pkgCache::DepIterator Start;
	 pkgCache::DepIterator End;
	 D.GlobOr(Start,End);

	 if (Cache->IsImportantDep(End) == false ||
	     (Cache[End] & pkgDepCache::DepGInstall) == pkgDepCache::DepGInstall)
	 {
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
	       out << " (" << Start.CompType() << " " << Start.TargetVer() << 
	       ")";
	    
	    /* Show a summary of the target package if possible. In the case
	       of virtual packages we show nothing */	 
	    pkgCache::PkgIterator Targ = Start.TargetPkg();
	    if (Targ->ProvidesList == 0)
	    {
	       out << _(" but ");
	       pkgCache::VerIterator Ver = Cache[Targ].InstVerIter(Cache);
	       if (Ver.end() == false)
		  out << Ver.VerStr() << (Now?_(" is installed"):_(" is to be installed"));
	       else
	       {
		  if (Cache[Targ].CandidateVerIter(Cache).end() == true)
		  {
		     if (Targ->ProvidesList == 0)
			out << _("it is not installable");
		     else
			out << _("it is a virtual package");
		  }		  
		  else
		     out << (Now?_("it is not installed"):_("it is not going to be installed"));
	       }
	    }
	    
	    if (Start != End)
	       cout << _(" or");
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
void ShowNew(ostream &out,CacheFile &Cache)
{
   /* Print out a list of packages that are going to be removed extra
      to what the user asked */
   string List;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      if (Cache[I].NewInstall() == true)
	 List += string(I.Name()) + " ";
   }
   
   ShowList(out,_("The following NEW packages will be installed:"),List);
}
									/*}}}*/
// ShowDel - Show packages to delete					/*{{{*/
// ---------------------------------------------------------------------
/* */
void ShowDel(ostream &out,CacheFile &Cache)
{
   /* Print out a list of packages that are going to be removed extra
      to what the user asked */
   string ListReplaced, ListRemoved;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      if (Cache[I].Delete() == true)
      {
         const char *suffix;
	 if ((Cache[I].iFlags & pkgDepCache::Purge) == pkgDepCache::Purge)
	    suffix = "* ";
	 else
	    suffix = " ";
	 if (Cache[I].Replaced())
	    ListReplaced += string(I.Name()) + suffix;
	 else
	    ListRemoved += string(I.Name()) + suffix;
      }
   }

   ShowList( out, _("The following packages will be REPLACED:"), ListReplaced );
   ShowList( out, _("The following packages will be REMOVED:"), ListRemoved );
}
									/*}}}*/
// ShowKept - Show kept packages					/*{{{*/
// ---------------------------------------------------------------------
/* */
void ShowKept(ostream &out,CacheFile &Cache)
{
   string List;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {	 
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      
      // Not interesting
      if (Cache[I].Upgrade() == true || Cache[I].Upgradable() == false ||
	  I->CurrentVer == 0 || Cache[I].Delete() == true)
	 continue;
      
      List += string(I.Name()) + " ";
   }
   ShowList(out,_("The following packages have been kept back"),List);
}
									/*}}}*/
// ShowUpgraded - Show upgraded packages				/*{{{*/
// ---------------------------------------------------------------------
/* */
void ShowUpgraded(ostream &out,CacheFile &Cache)
{
   string List;

   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      
      // Not interesting
      if (Cache[I].Upgrade() == false || Cache[I].NewInstall() == true)
	 continue;
      
      List += string(I.Name()) + " ";
   }
   ShowList(out,_("The following packages will be upgraded"),List);
}
									/*}}}*/
// ShowUpgradeSummary - Show upgraded packages with detailed summary    /*{{{*/
// ---------------------------------------------------------------------
/* */
void ShowUpgradeSummary(ostream &out,CacheFile &Cache)
{
   string List;
   bool first = true;
    

   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      const char *prio = NULL;
      const char *summ = NULL;
      const char *date = NULL;
      
      // Not interesting
      if (Cache[I].Upgrade() == false || Cache[I].NewInstall() == true)
	 continue;
      
      if (Cache.LoadRecord(Cache[I].InstVerIter(Cache)))
      {
	 prio = Cache.GetPriority();
	 summ = Cache.GetSummary();
	 date = Cache.GetDate();
      }
      
      if (first)
      {
	  out << _("The following packages can be upgraded:") << endl;
	  first = false;
      }
       
      if (!prio)
	  prio = "?";
       if (!date)
	  date = "?";

      out << endl << I.Name();
      if (I.CurrentVer() != 0)
	  out << _("  from  ") << I.CurrentVer().VerStr();
      out << _("  to  ") << Cache[I].InstVerIter(Cache).VerStr() << endl;

       out << _(" Importance: ") << prio << "  ";
       out << _(" Date: ") << date << endl;
      if (summ)
	 ShowText(out, summ);
   }
}
									/*}}}*/
// ShowHold - Show held but changed packages				/*{{{*/
// ---------------------------------------------------------------------
/* */
bool ShowHold(ostream &out,CacheFile &Cache)
{
   string List;
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      if (Cache[I].InstallVer != (pkgCache::Version *)I.CurrentVer() &&
	  I->SelectedState == pkgCache::State::Hold)
	 List += string(I.Name()) + " ";
   }

   return ShowList(out,_("The following held packages will be changed:"),List);
}
									/*}}}*/
// ShowEssential - Show an essential package warning			/*{{{*/
// ---------------------------------------------------------------------
/* This prints out a warning message that is not to be ignored. It shows
   all essential packages and their dependents that are to be removed. 
   It is insanely risky to remove the dependents of an essential package! */
bool ShowEssential(ostream &out,CacheFile &Cache)
{
   string List;
   bool *Added = new bool[Cache->HeaderP->PackageCount];
   for (unsigned int I = 0; I != Cache->HeaderP->PackageCount; I++)
      Added[I] = false;
   
   for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
   {
      pkgCache::PkgIterator I(Cache,Cache.List[J]);
      if ((I->Flags & pkgCache::Flag::Essential) != pkgCache::Flag::Essential)
	 continue;
      
      // The essential package is being removed
      if (Cache[I].Delete() == true)
      {
	 if (Added[I->ID] == false)
	 {
	    Added[I->ID] = true;
	    List += string(I.Name()) + " ";
	 }
      }
      
      if (I->CurrentVer == 0)
	 continue;

      // Print out any essential package depenendents that are to be removed
      // and won't be replaced by something new
      for (pkgDepCache::DepIterator D = I.CurrentVer().DependsList(); D.end() == false; D++)
      {
	 // Skip everything but depends
	 if (D->Type != pkgCache::Dep::PreDepends &&
	     D->Type != pkgCache::Dep::Depends)
	    continue;

	 pkgCache::Version **Targets = D.AllTargets();
	 
	 bool Broke = true;
	 
	 while (Targets != 0 && *Targets != 0)
	 {
	    pkgCache::PkgIterator P = pkgCache::VerIterator(*Cache,*Targets).ParentPkg();

	    if (Cache[P].Install() == true ||
		(P->CurrentVer != 0 && Cache[P].Delete() == false))
	    {
	       Broke = false;
	       break;
	    }
	    Targets++;
	 }
	 if (Broke == true)
	 {
	    pkgCache::PkgIterator P = D.SmartTargetPkg();
	    if (Added[P->ID] == true)
	       continue;
	    Added[P->ID] = true;
	    
	    char S[300];
	    sprintf(S,_("%s (due to %s) "),P.Name(),I.Name());
	    List += S;
	 }
      }      
   }
   
   delete [] Added;
   if (List.empty() == false)
      out << _("WARNING: The following essential packages will be removed") << endl;
   return ShowList(out,_("This should NOT be done unless you know exactly what you are doing!"),List);
}
									/*}}}*/
// Stats - Show some statistics						/*{{{*/
// ---------------------------------------------------------------------
/* */
void Stats(ostream &out,pkgDepCache &Dep)
{
   unsigned long Upgrade = 0;
   unsigned long Install = 0;
   unsigned long ReInstall = 0;
   for (pkgCache::PkgIterator I = Dep.PkgBegin(); I.end() == false; I++)
   {
      if (Dep[I].NewInstall() == true)
	 Install++;
      else
	 if (Dep[I].Upgrade() == true)
	    Upgrade++;
      if (Dep[I].Delete() == false && (Dep[I].iFlags & pkgDepCache::ReInstall) == pkgDepCache::ReInstall)
	 ReInstall++;
   }   

   out << Upgrade << _(" packages upgraded, ") <<
      Install << _(" newly installed, ");
   if (ReInstall != 0)
      out << ReInstall << _(" reinstalled, ");
   out << Dep.DelCount() << _(" to remove(replace) and ") <<
      Dep.KeepCount() << _(" not upgraded.") << endl;

   if (Dep.BadCount() != 0)
      out << Dep.BadCount() << _(" packages not fully installed or removed.") << endl;
}
									/*}}}*/

bool CacheFile::LoadRecord(pkgCache::VerIterator V)
{
   pkgCache::VerFileIterator Vf = V.FileList();
   for (; Vf.end() == false; Vf++)
      if ((Vf.File()->Flags & pkgCache::Flag::NotSource) == 0)
	 break;
   if (Vf.end() == true)
      Vf = V.FileList();
      
   // Check and load the package list file
   pkgCache::PkgFileIterator I = Vf.File();
   if (I.IsOk() == false)
      return _error->Error(_("Package file %s is out of sync."),I.FileName());
   
   FileFd PkgF(I.FileName(),FileFd::ReadOnly);
   if (_error->PendingError() == true)
      return false;
   
   // Read the record and then write it out again.
   if (PkgF.Seek(V.FileList()->Offset) == false)
      return false;

   if (header)
       headerFree(header);
	
   FD_t fdt = fdDup(PkgF.Fd());
   header = headerRead(fdt, HEADER_MAGIC_YES);
   fdClose(fdt);
   
   return true;
}


const char *CacheFile::GetDate()
{
   const char *s = NULL;
   
   if (header)
   {
      int type, count;
      
      headerGetEntry(header, CRPMTAG_UPDATE_DATE, &type, (void **)&s, &count);
   }
   return s;
}


const char *CacheFile::GetPriority()
{
   const char *s = NULL;
   
   if (header)
   {
      int type, count;
      
      headerGetEntry(header, CRPMTAG_UPDATE_IMPORTANCE, &type, (void **)&s, &count);
   }
   return s;
}


const char *CacheFile::GetSummary()
{
   const char *s = NULL;

   if (header)
   {
      int type, count;

      headerGetEntry(header, CRPMTAG_UPDATE_SUMMARY, &type, (void **)&s, &count);
   }
   return s;
}

// CacheFile::NameComp - QSort compare by name				/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgCache *CacheFile::SortCache = 0;
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
// CacheFile::Open - Open the cache file				/*{{{*/
// ---------------------------------------------------------------------
/* This routine generates the caches and then opens the dependency cache
   and verifies that the system is OK. */
bool CacheFile::CheckDeps(bool AllowBroken)
{
   if (_error->PendingError() == true)
      return false;

   // Check that the system is OK
   if (Cache->DelCount() != 0 || Cache->InstCount() != 0)
      return _error->Error("Internal Error, non-zero counts");
   
   // Apply corrections for half-installed packages
   if (pkgApplyStatus(*Cache) == false)
      return false;
   
   // Nothing is broken
   if (Cache->BrokenCount() == 0 || AllowBroken == true)
      return true;

   // Attempt to fix broken things
   if (_config->FindB("APT::Get::Fix-Broken",false) == true)
   {
      c1out << _("Correcting dependencies...") << flush;
      if (pkgFixBroken(*Cache) == false || Cache->BrokenCount() != 0)
      {
	 c1out << _(" failed.") << endl;
	 ShowBroken(c1out,*this,true);

	 return _error->Error(_("Unable to correct dependencies"));
      }
      if (pkgMinimizeUpgrade(*Cache) == false)
	 return _error->Error(_("Unable to minimize the upgrade set"));
      
      c1out << _(" Done") << endl;
   }
   else
   {
      c1out << _("You might want to run `apt-get -f install' to correct these.") << endl;
      ShowBroken(c1out,*this,true);

      return _error->Error(_("Unmet dependencies. Try using -f."));
   }
      
   return true;
}
									/*}}}*/

// InstallPackages - Actually download and install the packages		/*{{{*/
// ---------------------------------------------------------------------
/* This displays the informative messages describing what is going to 
   happen and then calls the download routines */
bool InstallPackages(CacheFile &Cache,bool ShwKept,bool Ask = true,
		     bool Saftey = true)
{
   pkgPackageManager *PM;
        
   if (_config->FindB("APT::Get::Purge",false) == true)
   {
      pkgCache::PkgIterator I = Cache->PkgBegin();
      for (; I.end() == false; I++)
      {
	 if (I.Purge() == false && Cache[I].Delete())
	    Cache->MarkDelete( I, Cache[I].Replaced(), true );
      }
   }
   
   bool Fail = false;
   bool Essential = false;
   
   // Show all the various warning indicators
   ShowDel(c1out,Cache);
   ShowNew(c1out,Cache);
   if (ShwKept == true)
      ShowKept(c1out,Cache);
   Fail |= !ShowHold(c1out,Cache);
   
   if (_config->FindB("APT::Get::Show-Upgrade-Summary",false) == true) {
      ShowUpgradeSummary(c1out,Cache);
   } else if (_config->FindB("APT::Get::Show-Upgraded",false) == true) {
      ShowUpgraded(c1out,Cache);
   }
   Essential = !ShowEssential(c1out,Cache);
   Fail |= Essential;
   Stats(c1out,Cache);
   
   if (_config->FindB("APT::Get::Show-Upgrade-Summary",false) == true) {
       return true;
   }
    
   // Sanity check
   if (Cache->BrokenCount() != 0)
   {
      ShowBroken(c1out,Cache,false);
      return _error->Error(_("Internal Error, InstallPackages was called with broken packages!"));
   }

   if (Cache->DelCount() == 0 && Cache->InstCount() == 0 &&
       Cache->BadCount() == 0)
      return true;

   // No remove flag
   if (Cache->DelCount() != 0 && _config->FindB("APT::Get::No-Remove",false) == true)
      return _error->Error(_("Packages need to be removed but No Remove was specified."));
       
   // Run the simulator ..
   if (_config->FindB("APT::Get::Simulate") == true)
   {
      pkgSimulate PM(Cache);
      pkgPackageManager::OrderResult Res = PM.DoInstall();
      if (Res == pkgPackageManager::Failed)
	 return false;
      if (Res != pkgPackageManager::Completed)
	 return _error->Error(_("Internal Error, Ordering didn't finish"));
      return true;
   }
   
   // Create the text record parser
   pkgRecords Recs(Cache);
   if (_error->PendingError() == true)
      return false;
   
   // Lock the archive directory
   FileFd Lock;
   if (_config->FindB("Debug::NoLocking",false) == false)
   {
      Lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
      if (_error->PendingError() == true)
	 return _error->Error(_("Unable to lock the download directory"));
   }
   
   // Create the download object
   AcqTextStatus Stat(ScreenWidth,_config->FindI("quiet",0));   
   pkgAcquire Fetcher(&Stat);

   // Read the source list
   pkgSourceList List;
   if (List.ReadMainList() == false)
      return _error->Error(_("The list of sources could not be read."));
   
   // Create the package manager and prepare to download

   PM = _system->CreatePackageManager(Cache);

   if (PM->GetArchives(&Fetcher,&List,&Recs) == false || 
       _error->PendingError() == true)
      return false;

   // Display statistics
   unsigned long FetchBytes = Fetcher.FetchNeeded();
   unsigned long FetchPBytes = Fetcher.PartialPresent();
   unsigned long DebBytes = Fetcher.TotalNeeded();
   if (DebBytes != Cache->DebSize())
   {
      c0out << DebBytes << ',' << Cache->DebSize() << endl;
      c0out << _("How odd.. The sizes didn't match, email apt@packages.debian.org") << endl;
   }
   
   // Number of bytes
   c1out << _("Need to get ");
   if (DebBytes != FetchBytes)
      c1out << SizeToStr(FetchBytes) << "B/" << SizeToStr(DebBytes) << 'B';
   else
      c1out << SizeToStr(DebBytes) << 'B';
      
   c1out << _(" of archives. After unpacking ");
   
   // Size delta
   if (Cache->UsrSize() > 0)
      c1out << SizeToStr(Cache->UsrSize()) << _("B will be used.");
   else if (Cache->UsrSize() < 0)
      c1out << SizeToStr(-1*Cache->UsrSize()) << _("B will be freed.");
   else /* if (Cache->UsrSize() == 0) */
      c1out << _("used disk space will remain the same.");
   c1out  << endl;

   if (_error->PendingError() == true)
      return false;

   /* Check for enough free space, but only if we are actually going to
      download */
   if (_config->FindB("APT::Get::Print-URIs") == false)
   {
      struct statvfs Buf;
      string OutputDir = _config->FindDir("Dir::Cache::Archives");
      if (statvfs(OutputDir.c_str(),&Buf) != 0)
	 return _error->Errno("statvfs",_("Couldn't determine free space in %s"),
			      OutputDir.c_str());
      if (unsigned(Buf.f_bfree) < (FetchBytes - FetchPBytes)/Buf.f_bsize)
	 return _error->Error(_("Sorry, you don't have enough free space in %s to hold all packages."),
			      OutputDir.c_str());
   }
   
   // Fail safe check
   if (_config->FindI("quiet",0) >= 2 ||
       _config->FindB("APT::Get::Assume-Yes",false) == true)
   {
      if (Fail == true && _config->FindB("APT::Get::Force-Yes",false) == false)
	 return _error->Error(_("There are problems and -y was used without --force-yes"));
   }         

   if (Essential == true && Saftey == true)
   {
      if (_config->FindB("APT::Get::Trivial-Only",false) == true)
	 return _error->Error(_("Trivial Only specified but this is not a trivial operation."));
      
      c2out << _("You are about to do something potentially harmful") << endl;
      c2out << _("To continue type in the phrase 'Yes, I understand this may be bad'") << endl;
      c2out << _(" ?] ") << flush;
      if (AnalPrompt(_("Yes, I understand this may be bad")) == false)
      {
	 c2out << _("Aborted.") << endl;
	 exit(1);
      }     
   }
   else
   {      
      // Prompt to continue
      if (Ask == true || Fail == true)
      {            
	 if (_config->FindB("APT::Get::Trivial-Only",false) == true)
	    return _error->Error(_("Trivial Only specified but this is not a trivial operation."));
	 
	 if (_config->FindI("quiet",0) < 2 &&
	     _config->FindB("APT::Get::Assume-Yes",false) == false)
	 {
	    c2out << _("Do you want to continue? [Y/n] ") << flush;
	 
	    if (YnPrompt() == false)
	    {
	       c2out << _("Aborted.") << endl;
	       exit(1);
	    }     
	 }	 
      }      
   }
   
   // Just print out the uris an exit if the --print-uris flag was used
   if (_config->FindB("APT::Get::Print-URIs") == true)
   {
      pkgAcquire::UriIterator I = Fetcher.UriBegin();
      for (; I != Fetcher.UriEnd(); I++)
	 cout << '\'' << I->URI << "' " << flNotDir(I->Owner->DestFile) << ' ' <<
	       I->Owner->FileSize << ' ' << I->Owner->MD5Sum() << endl;
      return true;
   }
   
   // Run it
   while (1)
   {
      bool Transient = false;
      if (_config->FindB("APT::Get::No-Download",false) == true)
      {
	 for (pkgAcquire::Item **I = Fetcher.ItemsBegin(); I < Fetcher.ItemsEnd();)
	 {
	    if ((*I)->Local == true)
	    {
	       I++;
	       continue;
	    }

	    // Close the item and check if it was found in cache
	    (*I)->Finished();
	    if ((*I)->Complete == false)
	       Transient = true;
	    
	    // Clear it out of the fetch list
	    delete *I;
	    I = Fetcher.ItemsBegin();
	 }	 
      }
      
      if (Fetcher.Run() == pkgAcquire::Failed)
	 return false;
      // Print out errors
      bool Failed = false;
      for (pkgAcquire::Item **I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
      {
	 if ((*I)->Status == pkgAcquire::Item::StatDone &&
	     (*I)->Complete == true)
	    continue;
	 
	 if ((*I)->Status == pkgAcquire::Item::StatIdle)
	 {
	    Transient = true;
	    // Failed = true;
	    continue;
	 }

	 cerr << _("Failed to fetch ") << (*I)->DescURI() << endl;
	 cerr << "  " << (*I)->ErrorText << endl;
	 Failed = true;
      }

      /* If we are in no download mode and missing files and there were
         'failures' then the user must specify -m. Furthermore, there 
         is no such thing as a transient error in no-download mode! */
      if (Transient == true &&
	  _config->FindB("APT::Get::No-Download",false) == true)
      {
	 Transient = false;
	 Failed = true;
      }
      
      if (_config->FindB("APT::Get::Download-Only",false) == true)
      {
	 if (Failed == true && _config->FindB("APT::Get::Fix-Missing",false) == false)
	    return _error->Error(_("Some files failed to download"));
	 return true;
      }
      
      if (Failed == true && _config->FindB("APT::Get::Fix-Missing",false) == false)
      {
	 return _error->Error(_("Unable to fetch some archives, maybe try with --fix-missing?"));
      }
      
      if (Transient == true && Failed == true)
	 return _error->Error(_("--fix-missing and media swapping is not currently supported"));
      
      // Try to deal with missing package files
      if (Failed == true && PM->FixMissing() == false)
      {
	 cerr << _("Unable to correct missing packages.") << endl;
	 return _error->Error(_("Aborting Install."));
      }

      // need this so that we first fetch everything and then install (for CDs)
      if (Transient == false || _config->FindB("Acquire::cdrom::copy", false) == false) {
	 Cache.ReleaseLock();
	 pkgPackageManager::OrderResult Res = PM->DoInstall();
	 if (Res == pkgPackageManager::Failed || _error->PendingError() == true)
	     return false;
	 if (Res == pkgPackageManager::Completed)
	     return true;
      }
      // Reload the fetcher object and loop again for media swapping
      Fetcher.Shutdown();
      if (PM->GetArchives(&Fetcher,&List,&Recs) == false)
	 return false;
   }
    
    if (_config->FindB("APT::Get::No-Download",false) == true)
	cout << _("Run apt-get clean to remove downloaded packages.") << endl;
}
									/*}}}*/
// TryToInstall - Try to install a single package			/*{{{*/
// ---------------------------------------------------------------------
/* This used to be inlined in DoInstall, but with the advent of regex package
   name matching it was split out.. */
bool TryToInstall(pkgCache::PkgIterator Pkg,pkgDepCache &Cache,
		  pkgProblemResolver &Fix,bool Remove,bool BrokenFix,
		  unsigned int &ExpectedInst,bool AllowFail = true)
{
   /* This is a pure virtual package and there is a single available 
      provides */
   if (Cache[Pkg].CandidateVer == 0 && Pkg->ProvidesList != 0 &&
       Pkg.ProvidesList()->NextProvides == 0)
   {
      pkgCache::PkgIterator Tmp = Pkg.ProvidesList().OwnerPkg();
      c1out << _("Note, selecting ") << Tmp.Name() << _(" instead of ") << Pkg.Name() << endl;
      Pkg = Tmp;
   }
   
   // Handle the no-upgrade case
   if (_config->FindB("APT::Get::no-upgrade",false) == true &&
       Pkg->CurrentVer != 0)
   {
      if (AllowFail == true)
	 c1out << _("Skipping ") << Pkg.Name() << _(", it is already installed and no-upgrade is set.") << endl;
      return true;
   }
   
   // Check if there is something at all to install
   pkgDepCache::StateCache &State = Cache[Pkg];
   if (Remove == true && Pkg->CurrentVer == 0)
   {
      if (AllowFail == false)
	 return false;
      return _error->Error(_("Package %s is not installed"),Pkg.Name());
   }
   
   if (State.CandidateVer == 0 && Remove == false)
   {
      if (AllowFail == false)
	 return false;
      
      if (Pkg->ProvidesList != 0)
      {
	 c1out << _("Package ") << Pkg.Name() << _(" is a virtual package provided by:") << endl;
	 
	 pkgCache::PrvIterator I = Pkg.ProvidesList();
	 for (; I.end() == false; I++)
	 {
	    pkgCache::PkgIterator Pkg = I.OwnerPkg();
	    
	    if (Cache[Pkg].CandidateVerIter(Cache) == I.OwnerVer())
	    {
	       if (Cache[Pkg].Install() == true && Cache[Pkg].NewInstall() == false)
		  c1out << "  " << Pkg.Name() << " " << I.OwnerVer().VerStr() <<
		  _(" [Installed]")<< endl;
	       else
		  c1out << "  " << Pkg.Name() << " " << I.OwnerVer().VerStr() << endl;
	    }      
	 }
	 c1out << _("You should explicitly select one to install.") << endl;
      }
      else
      {
	 c1out << _("Package ") << Pkg.Name() << _(" has no available version, but exists in the database.") << endl;
	 c1out << _("This typically means that the package was mentioned in a dependency and ") << endl;
	 c1out << _("never uploaded, has been obsoleted or is not available with the contents ") << endl;
	 c1out << _("of sources.list") << endl;
	 
	 string List;
	 pkgCache::DepIterator Dep = Pkg.RevDependsList();
	 for (; Dep.end() == false; Dep++)
	 {
	    if (Dep->Type != pkgCache::Dep::Replaces
		&& Dep->Type != pkgCache::Dep::Obsoletes)
	       continue;
	    List += string(Dep.ParentPkg().Name()) + " ";
	 }	    
	 ShowList(c1out,_("However the following packages replace it:"),List);
      }
      
      _error->Error(_("Package %s has no installation candidate"),Pkg.Name());
      return false;
   }

   Fix.Clear(Pkg);
   Fix.Protect(Pkg);   
   if (Remove == true)
   {
      Fix.Remove(Pkg);
      Cache.MarkDelete(Pkg,false,_config->FindB("APT::Get::Purge",false));
      return true;
   }
   
   // Install it
   Cache.MarkInstall(Pkg,false);
   if (State.Install() == false)
   {
      if (_config->FindB("APT::Get::ReInstall",false) == true)
      {
	 if (Pkg->CurrentVer == 0 || Pkg.CurrentVer().Downloadable() == false)
	    c1out << _("Sorry, re-installation of ") << Pkg.Name() << _(" is not possible, it cannot be downloaded") << endl;
	 else
	    Cache.SetReInstall(Pkg,true);
      }      
      else
      {
	 if (AllowFail == true)
	    c1out << _("Sorry, ") << Pkg.Name() << _(" is already the newest version") << endl;
      }      
   }   
   else
      ExpectedInst++;
   
   // Install it with autoinstalling enabled.
   if (State.InstBroken() == true && BrokenFix == false)
      Cache.MarkInstall(Pkg,true);
   return true;
}
									/*}}}*/

// DoUpdate - Update the package lists					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool DoUpdate(CommandLine &)
{
   // Get the source list
   pkgSourceList List;
   if (List.ReadMainList() == false)
      return false;

   // Lock the list directory
   FileFd Lock;
   if (_config->FindB("Debug::NoLocking",false) == false)
   {
      Lock.Fd(GetLock(_config->FindDir("Dir::State::Lists") + "lock"));
      if (_error->PendingError() == true)
	 return _error->Error(_("Unable to lock the list directory"));
   }
   
   // Create the download object
   AcqTextStatus Stat(ScreenWidth,_config->FindI("quiet",0));
   pkgAcquire Fetcher(&Stat);
   
   
   bool hasIndex = false;

   // Download the signed index hashfiles
   pkgSourceList::rep_iterator R;
   for (R = List.rep_begin(); R != List.rep_end(); R++)
   {
      if ((*R)->Vendor == NULL) {
	 continue;
      }      
      new pkgAcqHashes(&Fetcher,*R);
      if (_error->PendingError() == true)
	 return false;
      hasIndex = true;
   }

   // Run it
   if (hasIndex && Fetcher.Run() == pkgAcquire::Failed)
      return _error->Error(_("Could not retrieve digitally signed hash file"));


   bool Failed = false;
   for (pkgAcquire::Item **I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
   {
      if ((*I)->Status == pkgAcquire::Item::StatDone) {
	 continue;
      }

      (*I)->Finished();

      cerr << _("Failed to fetch hash file: ")
	  << (*I)->DescURI() << endl;
      cerr << "  " << (*I)->ErrorText << endl;
      Failed = true;
   }

   if (Failed) 
   {
      return _error->Error(_("Some of the signed hash files could not be retrieved. Aborting operation."));
   }
   
   pkgSourceList::const_iterator I;
   // Populate it with the source selection
   for (I = List.begin(); I != List.end(); I++)
   {
      new pkgAcqIndex(&Fetcher,I);
      if (_error->PendingError() == true)
	 return false;
   }

   // Run it
   if (Fetcher.Run() == pkgAcquire::Failed)
      return false;

   bool AuthFailed = false;
   Failed = false;
   for (pkgAcquire::Item **I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
   {
      if ((*I)->Status == pkgAcquire::Item::StatDone)
	 continue;
      
      if ((*I)->Status == pkgAcquire::Item::StatAuthError)
	 AuthFailed = true;

      (*I)->Finished();
      
      cerr << _("Failed to fetch ") << (*I)->DescURI() << endl;
      cerr << "  " << (*I)->ErrorText << endl;
      Failed = true;
   }
   
   // Clean out any old list files
   if (_config->FindB("APT::Get::List-Cleanup",true) == true)
   {
      if (Fetcher.Clean(_config->FindDir("Dir::State::lists")) == false ||
	  Fetcher.Clean(_config->FindDir("Dir::State::lists") + "partial/") == false)
	 return false;
   }
   
   if (AuthFailed == true)
      return _error->Error(_("Some of the index files had mismatching MD5 sums!"));
   
   // Prepare the cache.   
   CacheFile Cache;
   if (Cache.Open() == false)
      return false;
   
   if (Failed == true)
      return _error->Error(_("Some index files failed to download, they have been ignored, or old ones used instead."));

   for (R = List.rep_begin(); R != List.rep_end(); R++)
   {
      if ((*R)->Vendor == NULL) {
	 string msg = (*R)->URI + _(" will not be authenticated.");
	 _error->Warning(msg.c_str());
      }
   }
   return true;
}
									/*}}}*/
// DoUpgrade - Upgrade all packages					/*{{{*/
// ---------------------------------------------------------------------
/* Upgrade all packages without installing new packages or erasing old
   packages */
bool DoUpgrade(CommandLine &CmdL)
{
   CacheFile Cache;
   if (Cache.Open() == false || Cache.CheckDeps() == false)
      return false;

   // Do the upgrade
   if (pkgAllUpgrade(Cache) == false)
   {
      ShowBroken(c1out,Cache,false);
      return _error->Error(_("Internal Error, AllUpgrade broke stuff"));
   }
   
   return InstallPackages(Cache,true);
}
									/*}}}*/
// DoInstall - Install packages from the command line			/*{{{*/
// ---------------------------------------------------------------------
/* Install named packages */
bool DoInstall(CommandLine &CmdL)
{
   CacheFile Cache;
   if (Cache.Open() == false || Cache.CheckDeps(CmdL.FileSize() != 1) == false)
      return false;
   
   // Enter the special broken fixing mode if the user specified arguments
   bool BrokenFix = false;
   if (Cache->BrokenCount() != 0)
      BrokenFix = true;
   
   unsigned int ExpectedInst = 0;
   unsigned int Packages = 0;
   pkgProblemResolver Fix(Cache);
   
   bool DefRemove = false;
   if (strcasecmp(CmdL.FileList[0],"remove") == 0)
      DefRemove = true;
   
   for (const char **I = CmdL.FileList + 1; *I != 0; I++)
   {
      // Duplicate the string
      unsigned int Length = strlen(*I);
      char S[300];
      if (Length >= sizeof(S))
	 continue;
      strcpy(S,*I);
      
      // See if we are removing the package
      bool Remove = DefRemove;
      while (Cache->FindPkg(S).end() == true)
      {
	 // Handle an optional end tag indicating what to do
	 if (S[Length - 1] == '-')
	 {
	    Remove = true;
	    S[--Length] = 0;
	    continue;
	 }
	 
	 if (S[Length - 1] == '+')
	 {
	    Remove = false;
	    S[--Length] = 0;
	    continue;
	 }
	 break;
      }
      
      // Locate the package
      pkgCache::PkgIterator Pkg = Cache->FindPkg(S);
      Packages++;
      if (Pkg.end() == true)
      {
	 // Check if the name is a regex
	 const char *I;
	 for (I = S; *I != 0; I++)
	    if (*I == '.' || *I == '?' || *I == '*')
	       break;
	 if (*I == 0)
	    return _error->Error(_("Couldn't find package %s"),S);

	 // Regexs must always be confirmed
	 ExpectedInst += 1000;
	 
	 // Compile the regex pattern
	 regex_t Pattern;
	 int ercode;
	 ercode = regcomp(&Pattern,S,REG_EXTENDED | REG_ICASE | REG_NOSUB);
	 if (ercode != 0) {
	    char buffer[256];
	    int l;
	    
	    strcpy(buffer, _("Regex compilation error:"));
	    l = strlen(buffer);
	     
	    regerror(ercode, &Pattern, buffer+l, sizeof(buffer)-l);
	     
	    return _error->Error(buffer);
	 }
	 // Run over the matches
	 bool Hit = false;
	 for (Pkg = Cache->PkgBegin(); Pkg.end() == false; Pkg++)
	 {
	    if (regexec(&Pattern,Pkg.Name(),0,0,0) != 0)
	       continue;
	    
	    Hit |= TryToInstall(Pkg,Cache,Fix,Remove,BrokenFix,
				ExpectedInst,false);
	 }
	 regfree(&Pattern);
	 
	 if (Hit == false)
	    return _error->Error(_("Couldn't find package %s"),S);
      }
      else
      {
	 if (TryToInstall(Pkg,Cache,Fix,Remove,BrokenFix,ExpectedInst) == false)
	    return false;
      }
   }

   /* If we are in the Broken fixing mode we do not attempt to fix the
      problems. This is if the user invoked install without -f and gave
      packages */
   if (BrokenFix == true && Cache->BrokenCount() != 0)
   {
      c1out << _("You might want to run `apt-get -f install' to correct these:") << endl;
      ShowBroken(c1out,Cache,false);

      return _error->Error(_("Unmet dependencies. Try 'apt-get -f install' with no packages (or specify a solution)."));
   }
   
   // Call the scored problem resolver
   Fix.InstallProtect();
   if (Fix.Resolve(true) == false)
      _error->Discard();

   // Now we check the state of the packages,
   if (Cache->BrokenCount() != 0)
   {
      c1out << _("Some packages could not be installed. This may mean that you have") << endl;
      c1out << _("requested an impossible situation or if you are using the unstable") << endl;
      c1out << _("distribution that some required packages have not yet been created") << endl;
      c1out << _("or been moved out of Incoming.") << endl;
      if (Packages == 1)
      {
	 c1out << endl;
	 c1out << _("Since you only requested a single operation it is extremely likely that") << endl;
	 c1out << _("the package is simply not installable and a bug report against") << endl;
	 c1out << _("that package should be filed.") << endl;
      }

      c1out << _("The following information may help to resolve the situation:") << endl;
      c1out << endl;
      ShowBroken(c1out,Cache,false);
      return _error->Error(_("Sorry, broken packages"));
   }   
   
   /* Print out a list of packages that are going to be installed extra
      to what the user asked */
   if (Cache->InstCount() != ExpectedInst)
   {
      string List;
      for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
      {
	 pkgCache::PkgIterator I(Cache,Cache.List[J]);
	 if ((*Cache)[I].Install() == false)
	    continue;

	 const char **J;
	 for (J = CmdL.FileList + 1; *J != 0; J++)
	    if (strcmp(*J,I.Name()) == 0)
		break;
	 
	 if (*J == 0)
	    List += string(I.Name()) + " ";
      }
      
      ShowList(c1out,_("The following extra packages will be installed:"),List);
   }

   // See if we need to prompt
   if (Cache->InstCount() == ExpectedInst && Cache->DelCount() == 0)
      return InstallPackages(Cache,false,false);
   
   return InstallPackages(Cache,false);   
}
									/*}}}*/
// DoDistUpgrade - Automatic smart upgrader				/*{{{*/
// ---------------------------------------------------------------------
/* Intelligent upgrader that will install and remove packages at will */
bool DoDistUpgrade(CommandLine &CmdL)
{
   CacheFile Cache;
   if (Cache.Open() == false || Cache.CheckDeps() == false)
      return false;

   c0out << _("Calculating Upgrade... ") << flush;
   if (pkgDistUpgrade(*Cache) == false)
   {
      c0out << _("Failed") << endl;
      ShowBroken(c1out,Cache,false);
      return false;
   }
   
   c0out << _("Done") << endl;
   
   return InstallPackages(Cache,true);
}
									/*}}}*/
// DoDSelectUpgrade - Do an upgrade by following dselects selections	/*{{{*/
// ---------------------------------------------------------------------
/* Follows dselect's selections */
bool DoDSelectUpgrade(CommandLine &CmdL)
{
   CacheFile Cache;
   if (Cache.Open() == false || Cache.CheckDeps() == false)
      return false;
   
   // Install everything with the install flag set
   pkgCache::PkgIterator I = Cache->PkgBegin();
   for (;I.end() != true; I++)
   {
      /* Install the package only if it is a new install, the autoupgrader
         will deal with the rest */
      if (I->SelectedState == pkgCache::State::Install)
	 Cache->MarkInstall(I,false);
   }

   /* Now install their deps too, if we do this above then order of
      the status file is significant for | groups */
   for (I = Cache->PkgBegin();I.end() != true; I++)
   {
      /* Install the package only if it is a new install, the autoupgrader
         will deal with the rest */
      if (I->SelectedState == pkgCache::State::Install)
	 Cache->MarkInstall(I,true);
   }
   
   // Apply erasures now, they override everything else.
   for (I = Cache->PkgBegin();I.end() != true; I++)
   {
      // Remove packages 
      if (I->SelectedState == pkgCache::State::DeInstall ||
	  I->SelectedState == pkgCache::State::Purge)
	 Cache->MarkDelete(I,false,I->SelectedState == pkgCache::State::Purge);
   }

   /* Resolve any problems that dselect created, allupgrade cannot handle
      such things. We do so quite agressively too.. */
   if (Cache->BrokenCount() != 0)
   {      
      pkgProblemResolver Fix(Cache);

      // Hold back held packages.
      if (_config->FindB("APT::Ignore-Hold",false) == false)
      {
	 for (pkgCache::PkgIterator I = Cache->PkgBegin(); I.end() == false; I++)
	 {
	    if (I->SelectedState == pkgCache::State::Hold)
	    {
	       Fix.Protect(I);
	       Cache->MarkKeep(I);
	    }
	 }
      }
   
      if (Fix.Resolve() == false)
      {
	 ShowBroken(c1out,Cache,false);
	 return _error->Error(_("Internal Error, problem resolver broke stuff"));
      }
   }

   // Now upgrade everything
   if (pkgAllUpgrade(Cache) == false)
   {
      ShowBroken(c1out,Cache,false);
      return _error->Error(_("Internal Error, problem resolver broke stuff"));
   }
   
   return InstallPackages(Cache,false);
}
									/*}}}*/
// DoClean - Remove download archives					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool DoClean(CommandLine &CmdL)
{
   if (_config->FindB("APT::Get::Simulate") == true)
   {
      cout << "Del " << _config->FindDir("Dir::Cache::archives") << "* " <<
	 _config->FindDir("Dir::Cache::archives") << "partial/*" << endl;
      return true;
   }
   
   // Lock the archive directory
   FileFd Lock;
   if (_config->FindB("Debug::NoLocking",false) == false)
   {
      Lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
      if (_error->PendingError() == true)
	 return _error->Error(_("Unable to lock the download directory"));
   }
   
   pkgAcquire Fetcher;
   Fetcher.Clean(_config->FindDir("Dir::Cache::archives"));
   Fetcher.Clean(_config->FindDir("Dir::Cache::archives") + "partial/");
   return true;
}
									/*}}}*/
// DoAutoClean - Smartly remove downloaded archives			/*{{{*/
// ---------------------------------------------------------------------
/* This is similar to clean but it only purges things that cannot be 
   downloaded, that is old versions of cached packages. */
class LogCleaner : public pkgArchiveCleaner
{
   protected:
   virtual void Erase(const char *File,string Pkg,string Ver,struct stat &St) 
   {
      c1out << "Del " << Pkg << " " << Ver << " [" << SizeToStr(St.st_size) << "B]" << endl;
      
      if (_config->FindB("APT::Get::Simulate") == false)
	 unlink(File);      
   };
};

bool DoAutoClean(CommandLine &CmdL)
{
   // Lock the archive directory
   FileFd Lock;
   if (_config->FindB("Debug::NoLocking",false) == false)
   {
      Lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
      if (_error->PendingError() == true)
	 return _error->Error(_("Unable to lock the download directory"));
   }
   
   CacheFile Cache;
   if (Cache.Open() == false)
      return false;
   
   LogCleaner Cleaner;
   
   return Cleaner.Go(_config->FindDir("Dir::Cache::archives"),*Cache) &&
      Cleaner.Go(_config->FindDir("Dir::Cache::archives") + "partial/",*Cache);
}
									/*}}}*/
// DoCheck - Perform the check operation				/*{{{*/
// ---------------------------------------------------------------------
/* Opening automatically checks the system, this command is mostly used
   for debugging */
bool DoCheck(CommandLine &CmdL)
{
   CacheFile Cache;
   Cache.Open();
   Cache.CheckDeps();
   
   return true;
}
									/*}}}*/
// DoSource - Fetch a source archive					/*{{{*/
// ---------------------------------------------------------------------
/* Fetch souce packages */
struct DscFile
{
   string Package;
   string Version;
   string Dsc;
};

bool DoSource(CommandLine &CmdL)
{
   CacheFile Cache;
    
    
   if (Cache.Open(false) == false)
      return false;

   if (CmdL.FileSize() <= 1)
      return _error->Error(_("Must specify at least one package to fetch source for"));
   
   // Read the source list
   pkgSourceList List;
   if (List.ReadMainList() == false)
      return _error->Error(_("The list of sources could not be read."));
   
   // Create the text record parsers
   pkgRecords Recs(Cache);
   pkgSrcRecords SrcRecs(List);
   if (_error->PendingError() == true)
      return false;

   // Create the download object
   AcqTextStatus Stat(ScreenWidth,_config->FindI("quiet",0));   
   pkgAcquire Fetcher(&Stat);

   DscFile *Dsc = new DscFile[CmdL.FileSize()];
   
   // Load the requestd sources into the fetcher
   unsigned J = 0;
   for (const char **I = CmdL.FileList + 1; *I != 0; I++, J++)
   {
      string Src;
      
      /* Lookup the version of the package we would install if we were to
         install a version and determine the source package name, then look
         in the archive for a source package of the same name. In theory
         we could stash the version string as well and match that too but
         today there aren't multi source versions in the archive. */
      pkgCache::PkgIterator Pkg = Cache->FindPkg(*I);
      if (Pkg.end() == false)
      {
	 pkgCache::VerIterator Ver = Cache->GetCandidateVer(Pkg);
	 if (Ver.end() == false)
	 {
	    pkgRecords::Parser &Parse = Recs.Lookup(Ver.FileList());
	    Src = Parse.SourcePkg();
	 }	 
      }   

      // No source package name..
      if (Src.empty() == true)
	 Src = *I;
      
      // The best hit
      pkgSrcRecords::Parser *Last = 0;
      unsigned long Offset = 0;
      string Version;
      bool IsMatch = false;
	 
      // Iterate over all of the hits
      pkgSrcRecords::Parser *Parse;
      SrcRecs.Restart();
      while ((Parse = SrcRecs.Find(Src.c_str(),false)) != 0)
      {
	 string Ver = Parse->Version();
	 
	 // Skip name mismatches
	 if (IsMatch == true && Parse->Package() != Src)
	    continue;

	 // Newer version or an exact match
	 if (Last == 0 || _system->versionCompare(Version,Ver) < 0 ||
	     (Parse->Package() == Src && IsMatch == false))
	 {
	    IsMatch = Parse->Package() == Src;
	    Last = Parse;
	    Offset = Parse->Offset();
	    Version = Ver;
	 }      
      }
      
      if (Last == 0)
	 return _error->Error(_("Unable to find a source package for %s"),Src.c_str());
      
      
      // Back track
      vector<pkgSrcRecords::File> Lst;
      if (Last->Jump(Offset) == false || Last->Files(Lst) == false)
	 return false;

      // Load them into the fetcher
      for (vector<pkgSrcRecords::File>::const_iterator I = Lst.begin();
	   I != Lst.end(); I++)
      {
	 string Comp;
#if 1
	 Comp = "srpm";
	 Dsc[J].Package = flNotDir(I->Path);
#else
	 // Try to guess what sort of file it is we are getting.
	 if (I->Path.find(".dsc") != string::npos)
	 {
	    Comp = "dsc";
	    Dsc[J].Package = Last->Package();
	    Dsc[J].Version = Last->Version();
	    Dsc[J].Dsc = flNotDir(I->Path);
	 }
	 
	 if (I->Path.find(".tar.gz") != string::npos)
	    Comp = "tar";
	 if (I->Path.find(".diff.gz") != string::npos)
	    Comp = "diff";
	 
	 // Diff only mode only fetches .diff files
	 if (_config->FindB("APT::Get::Diff-Only",false) == true &&
	     Comp != "diff")
	    continue;
	 
	 // Tar only mode only fetches .tar files
	 if (_config->FindB("APT::Get::Tar-Only",false) == true &&
	     Comp != "tar")
	    continue;
#endif
	 new pkgAcqFile(&Fetcher,Last->Source()->ArchiveURI(I->Path),
			I->MD5Hash,I->Size,
			Last->Source()->SourceInfo(Src,Last->Version(),Comp),
			Src);
      }
   }
   
   // Display statistics
   unsigned long FetchBytes = Fetcher.FetchNeeded();
   unsigned long FetchPBytes = Fetcher.PartialPresent();
   unsigned long DebBytes = Fetcher.TotalNeeded();

   // Check for enough free space
   struct statvfs Buf;
   string OutputDir = ".";
   if (statvfs(OutputDir.c_str(),&Buf) != 0)
      return _error->Errno("statvfs",_("Couldn't determine free space in %s"),
			   OutputDir.c_str());
   if (unsigned(Buf.f_bfree) < (FetchBytes - FetchPBytes)/Buf.f_bsize)
      return _error->Error(_("Sorry, you don't have enough free space in %s"),
			   OutputDir.c_str());
   
   // Number of bytes
   c1out << _("Need to get ");
   if (DebBytes != FetchBytes)
      c1out << SizeToStr(FetchBytes) << "B/" << SizeToStr(DebBytes) << 'B';
   else
      c1out << SizeToStr(DebBytes) << 'B';
   c1out << _(" of source archives.") << endl;

   if (_config->FindB("APT::Get::Simulate",false) == true)
   {
      for (unsigned I = 0; I != J; I++)
	 cout << _("Fetch Source ") << Dsc[I].Package << endl;
      return true;
   }
   
   // Just print out the uris an exit if the --print-uris flag was used
   if (_config->FindB("APT::Get::Print-URIs") == true)
   {
      pkgAcquire::UriIterator I = Fetcher.UriBegin();
      for (; I != Fetcher.UriEnd(); I++)
	 cout << '\'' << I->URI << "' " << flNotDir(I->Owner->DestFile) << ' ' <<
	       I->Owner->FileSize << ' ' << I->Owner->MD5Sum() << endl;
      return true;
   }
   
   // Run it
   if (Fetcher.Run() == pkgAcquire::Failed)
      return false;

   // Print error messages
   bool Failed = false;
   for (pkgAcquire::Item **I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
   {
      if ((*I)->Status == pkgAcquire::Item::StatDone &&
	  (*I)->Complete == true)
	 continue;
      
      cerr << _("Failed to fetch ") << (*I)->DescURI() << endl;
      cerr << "  " << (*I)->ErrorText << endl;
      Failed = true;
   }
   if (Failed == true)
      return _error->Error(_("Failed to fetch some archives."));
   
   if (_config->FindB("APT::Get::Download-only",false) == true)
      return true;
   
   // Unpack the sources
   pid_t Process = ExecFork();
   
   if (Process == 0)
   {
      for (unsigned I = 0; I != J; I++)
      {
#if 1
	 char S[400];

	 if (_config->FindB("APT::Get::Compile",false) == true) 
	 {
	    snprintf(S,sizeof(S),"rpm --rebuild %s", Dsc[I].Package.c_str());
	    if (system(S) != 0)
	    {
	       cerr << _("Build command '") << S << _("' failed.") << endl;
	       _exit(1);
	    }
	 }
#else
	 string Dir = Dsc[I].Package + '-' + _system->baseVersion(Dsc[I].Version.c_str());

	 // Diff only mode only fetches .diff files
	 if (_config->FindB("APT::Get::Diff-Only",false) == true ||
	     _config->FindB("APT::Get::Tar-Only",false) == true ||
	     Dsc[I].Dsc.empty() == true)
	    continue;

	 // See if the package is already unpacked
	 struct stat Stat;
	 if (stat(Dir.c_str(),&Stat) == 0 &&
	     S_ISDIR(Stat.st_mode) != 0)
	 {
	    c0out << _("Skipping unpack of already unpacked source in ") << Dir << endl;
	 }
	 else
	 {
	    // Call dpkg-source
	    char S[500];
	    snprintf(S,sizeof(S),"%s -x %s",
		     _config->Find("Dir::Bin::dpkg-source","dpkg-source").c_str(),
		     Dsc[I].Dsc.c_str());
	    if (system(S) != 0)
	    {
	       cerr << _("Unpack command '") << S << _("' failed.") << endl;
	       _exit(1);
	    }	    
	 }
	 
	 // Try to compile it with dpkg-buildpackage
	 if (_config->FindB("APT::Get::Compile",false) == true)
	 {
	    char S[500];	     
	    // Call dpkg-buildpackage
	    snprintf(S,sizeof(S),"cd %s && %s %s",
		     Dir.c_str(),
		     _config->Find("Dir::Bin::dpkg-buildpackage","dpkg-buildpackage").c_str(),
		     _config->Find("DPkg::Build-Options","-b -uc").c_str());
	    
	    if (system(S) != 0)
	    {
	       cerr << _("Build command '") << S << _("' failed.") << endl;
	       _exit(1);
	    }
	 }
#endif
      }
      
      _exit(0);
   }
   
   // Wait for the subprocess
   int Status = 0;
   while (waitpid(Process,&Status,0) != Process)
   {
      if (errno == EINTR)
	 continue;
      return _error->Errno("waitpid",_("Couldn't wait for subprocess"));
   }

   if (WIFEXITED(Status) == 0 || WEXITSTATUS(Status) != 0)
      return _error->Error(_("Child process failed"));
   
   return true;
}
									/*}}}*/

// ShowHelp - Show a help screen					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool ShowHelp(CommandLine &CmdL)
{
   cout << PACKAGE << ' ' << VERSION << " for " << COMMON_CPU <<
       " compiled on " << __DATE__ << "  " << __TIME__ << endl;
   if (_config->FindB("version") == true)
      return 100;
       
   cout << _("Usage: apt-get [options] command") << endl;
   cout << _("       apt-get [options] install pkg1 [pkg2 ...]") << endl;
   cout << endl;
   cout << _("apt-get is a simple command line interface for downloading and") << endl;
   cout << _("installing packages. The most frequently used commands are update") << endl;
   cout << _("and install.") << endl;   
   cout << endl;
   cout << _("Commands:") << endl;
   cout << _("   update - Retrieve new lists of packages") << endl;
   cout << _("   upgrade - Perform an upgrade") << endl;
   cout << _("   install - Install new packages") << endl;
   cout << _("   remove - Remove packages") << endl;
   cout << _("   source - Download source archives") << endl;
   cout << _("   dist-upgrade - Distribution upgrade, see apt-get(8)") << endl;
//   cout << "   dselect-upgrade - Follow dselect selections" << endl;
   cout << _("   clean - Erase downloaded archive files") << endl;
   cout << _("   autoclean - Erase old downloaded archive files") << endl;
   cout << _("   check - Verify that there are no broken dependencies") << endl;
   cout << endl;
   cout << _("Options:") << endl;
   cout << _("  -h  This help text.") << endl;
   cout << _("  -q  Loggable output - no progress indicator") << endl;
   cout << _("  -qq No output except for errors") << endl;
   cout << _("  -S  Show summary for upgrade operation and quit") << endl;
   cout << _("  -d  Download only - do NOT install or unpack archives") << endl;
   cout << _("  -s  No-act. Perform ordering simulation") << endl;
   cout << _("  -y  Assume Yes to all queries and do not prompt") << endl;
   cout << _("  -f  Attempt to continue if the integrity check fails") << endl;
   cout << _("  -m  Attempt to continue if archives are unlocatable") << endl;
   cout << _("  -u  Show a list of upgraded packages as well") << endl;
   cout << _("  -b  Build the source package after fetching it") << endl;
   cout << _("  -K  Verify signatures in individual packages and quit") << endl;
   cout << _("  -c=? Read this configuration file") << endl;
   cout << _("  -o=? Set an arbitary configuration option, eg -o dir::cache=/tmp") << endl;
   cout << _("See the apt-get(8), sources.list(5) and apt.conf(5) manual") << endl;
   cout << _("pages for more information and options.") << endl;
   return 100;
}
									/*}}}*/
// GetInitialize - Initialize things for apt-get			/*{{{*/
// ---------------------------------------------------------------------
/* */
void GetInitialize()
{
   _config->Set("quiet",0);
   _config->Set("help",false);
   _config->Set("APT::Get::Download-Only",false);
   _config->Set("APT::Get::Simulate",false);
   _config->Set("APT::Get::Assume-Yes",false);
   _config->Set("APT::Get::Fix-Broken",false);
   _config->Set("APT::Get::Force-Yes",false);
   _config->Set("APT::Get::APT::Get::No-List-Cleanup",true);
}
									/*}}}*/
// SigWinch - Window size change signal handler				/*{{{*/
// ---------------------------------------------------------------------
/* */
void SigWinch(int)
{
   // Riped from GNU ls
#ifdef TIOCGWINSZ
   struct winsize ws;
  
   if (ioctl(1, TIOCGWINSZ, &ws) != -1 && ws.ws_col >= 5)
      ScreenWidth = ws.ws_col - 1;
#endif
}
									/*}}}*/

int main(int argc,const char *argv[])
{
   CommandLine::Args Args[] = {
      {'h',"help","help",0},
      {'v',"version","version",0},
      {'q',"quiet","quiet",CommandLine::IntLevel},
      {'q',"silent","quiet",CommandLine::IntLevel},
      {'d',"download-only","APT::Get::Download-Only",0},
      {'b',"compile","APT::Get::Compile",0},
      {'b',"build","APT::Get::Compile",0},
      {'s',"simulate","APT::Get::Simulate",0},
      {'s',"just-print","APT::Get::Simulate",0},
      {'s',"recon","APT::Get::Simulate",0},
      {'s',"no-act","APT::Get::Simulate",0},
      {'S',"summary","APT::Get::Show-Upgrade-Summary",0},
      {'y',"yes","APT::Get::Assume-Yes",0},
      {'y',"assume-yes","APT::Get::Assume-Yes",0},      
      {'f',"fix-broken","APT::Get::Fix-Broken",0},
      {'u',"show-upgraded","APT::Get::Show-Upgraded",0},
      {'m',"ignore-missing","APT::Get::Fix-Missing",0},
      {0,"no-download","APT::Get::No-Download",0},
      {0,"fix-missing","APT::Get::Fix-Missing",0},
      {0,"ignore-hold","APT::Ingore-Hold",0},      
      {0,"no-upgrade","APT::Get::no-upgrade",0},
      {0,"force-yes","APT::Get::force-yes",0},
      {0,"print-uris","APT::Get::Print-URIs",0},
      {0,"diff-only","APT::Get::Diff-Only",0},
      {0,"tar-only","APT::Get::tar-Only",0},
      {0,"purge","APT::Get::Purge",0},
      {0,"list-cleanup","APT::Get::List-Cleanup",0},
      {0,"reinstall","APT::Get::ReInstall",0},
      {0,"trivial-only","APT::Get::Trivial-Only",0},
      {0,"no-remove","APT::Get::No-Remove",0},
      {'c',"config-file",0,CommandLine::ConfigFile},
      {'o',"option",0,CommandLine::ArbItem},
      {'K',"check-signatures","RPM::Check-Signatures",0},
      {0,0,0,0}};
   CommandLine::Dispatch Cmds[] = {{"update",&DoUpdate},
                                   {"upgrade",&DoUpgrade},
                                   {"install",&DoInstall},
                                   {"remove",&DoInstall},
                                   {"dist-upgrade",&DoDistUpgrade},
//                                   {"dselect-upgrade",&DoDSelectUpgrade},
                                   {"clean",&DoClean},
                                   {"autoclean",&DoAutoClean},
                                   {"check",&DoCheck},
      				   {"source",&DoSource},
      				   {"help",&ShowHelp},
                                   {0,0}};
   
       
   setlocale(LC_ALL, "");
   bindtextdomain(PACKAGE, LOCALEDIR);
   textdomain(PACKAGE);

   // Parse the command line and initialize the package library
   CommandLine CmdL(Args,_config);
   if (pkgInitialize(*_config) == false ||
       CmdL.Parse(argc,argv) == false)
   {
      _error->DumpErrors();
      return 100;
   }
   
   if (1) {
      RPMFactory *factory = new RPMFactory; // uses a config option, so must come after
      void *shutup_gcc = NULL;
      shutup_gcc = factory;
   }
#if 0 //akk
   else {
      DebianFactory *factory = new DebianFactory;
      void *shutup_gcc = NULL;
      shutup_gcc = factory;
   }
#endif
   // See if the help should be shown
   if (_config->FindB("help") == true ||
       _config->FindB("version") == true ||
       CmdL.FileSize() == 0)
      return ShowHelp(CmdL);
   
   // Deal with stdout not being a tty
   if (ttyname(STDOUT_FILENO) == 0 && _config->FindI("quiet",0) < 1)
      _config->Set("quiet","1");

   // Setup the output streams
   c0out.rdbuf(cout.rdbuf());
   c1out.rdbuf(cout.rdbuf());
   c2out.rdbuf(cout.rdbuf());
   if (_config->FindI("quiet",0) > 0)
      c0out.rdbuf(devnull.rdbuf());
   if (_config->FindI("quiet",0) > 1)
      c1out.rdbuf(devnull.rdbuf());

   // Setup the signals
   signal(SIGPIPE,SIG_IGN);
   signal(SIGWINCH,SigWinch);
   SigWinch(0);

   // Match the operation
   CmdL.DispatchArg(Cmds);
    
#ifdef DEBUG
    {
        struct mallinfo ma = mallinfo();
        printf("Total allocated memory: %i kB. Total memory in use: %i kB.\n",
                (ma.arena+ma.hblkhd)/1024, (ma.uordblks+ma.hblkhd)/1024);
        
    }
#endif

   // Print any errors or warnings found during parsing
   if (_error->empty() == false)
   {
      bool Errors = _error->PendingError();
      _error->DumpErrors();
      return Errors == true?100:0;
   }
   
   return 0;   
}
