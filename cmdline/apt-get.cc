// Description								/*{{{*/
// $Id: apt-get.cc,v 1.144 2003/10/29 17:56:31 mdz Exp $
/* ######################################################################

   apt-get - Cover for dpkg

   This is an allout cover for dpkg implementing a safer front end. It is
   based largely on libapt-pkg.

   The syntax is different,
      apt-get [opt] command [things]
   Where command is:
      update - Resyncronize the package files from their sources
      upgrade - Smart-Download the newest versions of all packages;
                since it may leave system in a broken state, upgrade is disabled
                by default unless --enable-upgrade option is used.
                Using dist-upgrade instead of upgrade is advised.
      dselect-upgrade - Follows dselect's changes to the Status: field
                       and installes new and removes old packages
      dist-upgrade - Powerfull upgrader designed to handle the issues with
                    a new distribution.
      install - Download and install a given package (by name, not by .deb)
      check - Update the package cache and check for broken packages
      clean - Erase the .debs downloaded to /var/cache/apt/archives and
              the partial dir too

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

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
#include <apt-pkg/version.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/sptr.h>
#include <apt-pkg/update.h>
#include <apt-pkg/versionmatch.h>

#include <apti18n.h>

#include "acqprogress.h"

// CNC:2003-02-14 - apti18n.h includes libintl.h which includes locale.h,
//		    as reported by Radu Greab.
//#include <locale.h>
#include <langinfo.h>
#include <fstream>
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

// CNC:2003-03-18
#include <apt-pkg/luaiface.h>

									/*}}}*/

using namespace std;

ostream c0out(0);
ostream c1out(0);
ostream c2out(0);
ostream c3out(0); // script output stream
ofstream devnull("/dev/null");
unsigned int ScreenWidth = 80;

// CNC:2003-03-19
#ifdef WITH_LUA
class AptGetLuaCache : public LuaCacheControl
{
public:
   std::unique_ptr<CacheFile> Cache;

   virtual pkgDepCache* Open(bool Write) override
   {
      if (!Cache)
      {
         Cache.reset(new CacheFile(c1out,Write));
         if (!Cache->Open())
         {
            return NULL;
         }

         if (!Cache->CheckDeps())
         {
            return NULL;
         }
      }

      return *Cache;
   }

   virtual void Close() override
   {
      Cache.reset();
   }
};
#endif

// YnPrompt - Yes No Prompt.						/*{{{*/
// ---------------------------------------------------------------------
/* Returns true on a Yes.*/
bool YnPrompt()
{
   if (_config->FindB("APT::Get::Assume-Yes",false) == true)
   {
      c1out << _("Y") << endl;
      return true;
   }

   char response[1024] = "";
   cin.getline(response, sizeof(response));

   if (!cin)
      return false;

   if (strlen(response) == 0)
      return true;

   regex_t Pattern;
   int Res;

   Res = regcomp(&Pattern, nl_langinfo(YESEXPR),
                 REG_EXTENDED|REG_ICASE|REG_NOSUB);

   if (Res != 0) {
      char Error[300];
      regerror(Res,&Pattern,Error,sizeof(Error));
      return _error->Error(_("Regex compilation error - %s"),Error);
   }

   Res = regexec(&Pattern, response, 0, NULL, 0);
   if (Res == 0)
      return true;
   return false;
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
// CNC:2003-03-06
// CheckOnly - Check if the cache has any changes to be applied		/*{{{*/
// ---------------------------------------------------------------------
/* Returns true if CheckOnly is active. */
bool CheckOnly(CacheFile &Cache)
{
   if (_config->FindB("APT::Get::Check-Only", false) == false)
      return false;
   if (Cache->InstCount() != 0 || Cache->DelCount() != 0) {
      if (_config->FindB("APT::Get::Show-Upgraded",true) == true)
	 ShowUpgraded(c1out,c3out,Cache,nullptr,ScreenWidth);
      ShowDel(c1out,c3out,Cache,nullptr,ScreenWidth);
      ShowNew(c1out,c3out,Cache,nullptr,ScreenWidth);
      //ShowKept(c1out,Cache);
      ShowHold(c1out,c3out,Cache,nullptr,ScreenWidth);
      ShowDowngraded(c1out,c3out,Cache,nullptr,ScreenWidth);
      ShowEssential(c1out,c3out,Cache,nullptr,ScreenWidth);
      Stats(c1out,c3out,Cache,nullptr);
      _error->Error(_("There are changes to be made"));
   }

   return true;
}
									/*}}}*/

// CNC:2002-07-06
bool DoClean(CommandLine &CmdL);
bool DoAutoClean(CommandLine &CmdL);

// InstallPackages - Actually download and install the packages		/*{{{*/
// ---------------------------------------------------------------------
/* This displays the informative messages describing what is going to
   happen and then calls the download routines */
bool InstallPackages(CacheFile &Cache,bool ShwKept,bool Ask = true,
		     bool Saftey = true)
{
   if (_config->FindB("APT::Get::Purge",false) == true)
   {
      pkgCache::PkgIterator I = Cache->PkgBegin();
      for (; I.end() == false; I++)
      {
	 if (I.Purge() == false && Cache[I].Mode == pkgDepCache::ModeDelete)
	    Cache->MarkDelete(I,true);
      }
   }

   bool Fail = false;
   bool Essential = false;

   // Show all the various warning indicators
   // CNC:2002-03-06 - Change Show-Upgraded default to true, and move upwards.
   if (_config->FindB("APT::Get::Show-Upgraded",true) == true)
      ShowUpgraded(c1out,c3out,Cache,nullptr,ScreenWidth);
   ShowDel(c1out,c3out,Cache,nullptr,ScreenWidth);
   ShowNew(c1out,c3out,Cache,nullptr,ScreenWidth);
   if (ShwKept == true)
      ShowKept(c1out,c3out,Cache,nullptr,ScreenWidth);
   Fail |= !ShowHold(c1out,c3out,Cache,nullptr,ScreenWidth);
   Fail |= !ShowDowngraded(c1out,c3out,Cache,nullptr,ScreenWidth);
   if (_config->FindB("APT::Get::Download-Only",false) == false)
        Essential = !ShowEssential(c1out,c3out,Cache,nullptr,ScreenWidth);
   Fail |= Essential;
   Stats(c1out,c3out,Cache,nullptr);

   // Sanity check
   if (Cache->BrokenCount() != 0)
   {
      ShowBroken(cerr,Cache,false);
      return _error->Error("Internal Error, InstallPackages was called with broken packages!");
   }

   if (Cache->DelCount() == 0 && Cache->InstCount() == 0 &&
       Cache->BadCount() == 0)
      return true;

   // No remove flag
   if (Cache->DelCount() != 0 && _config->FindB("APT::Get::Remove",true) == false)
      return _error->Error(_("Packages need to be removed but Remove is disabled."));

   // Run the simulator ..
   if (_config->FindB("APT::Get::Simulate") == true)
   {
      pkgSimulate PM(Cache);
      pkgPackageManager::OrderResult Res = PM.DoInstall();
      if (Res == pkgPackageManager::Failed)
	 return false;
      if (Res != pkgPackageManager::Completed)
	 return _error->Error("Internal Error, Ordering didn't finish");
      return true;
   }

   // Create the text record parser
   pkgRecords Recs(Cache);
   if (_error->PendingError() == true)
      return false;

   // Lock the archive directory
   FileFd Lock;
   if (_config->FindB("Debug::NoLocking",false) == false &&
       _config->FindB("APT::Get::Print-URIs") == false)
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
   const SPtr<pkgPackageManager> PM(_system->CreatePM(Cache));
   if (PM->GetArchives(&Fetcher,&List,&Recs) == false ||
       _error->PendingError() == true)
      return false;

   // Display statistics
   double FetchBytes = Fetcher.FetchNeeded();
   double FetchPBytes = Fetcher.PartialPresent();
   double DebBytes = Fetcher.TotalNeeded();
   if (DebBytes != Cache->DebSize())
   {
      c0out << DebBytes << ',' << Cache->DebSize() << endl;
      c0out << "How odd.. The sizes didn't match, email apt@packages.debian.org" << endl;
   }

   // Number of bytes
   if (DebBytes != FetchBytes)
      ioprintf(c1out,_("Need to get %sB/%sB of archives.\n"),
	       SizeToStr(FetchBytes).c_str(),SizeToStr(DebBytes).c_str());
   else
      ioprintf(c1out,_("Need to get %sB of archives.\n"),
	       SizeToStr(DebBytes).c_str());

   // Size delta
   if (Cache->UsrSize() >= 0)
      ioprintf(c1out,_("After unpacking %sB of additional disk space will be used.\n"),
	       SizeToStr(Cache->UsrSize()).c_str());
   else
      ioprintf(c1out,_("After unpacking %sB disk space will be freed.\n"),
	       SizeToStr(-1*Cache->UsrSize()).c_str());
   c3out<<"apt-get:status:disk-size:"<<SizeToStr(Cache->UsrSize())<<endl;

   if (_error->PendingError() == true)
      return false;

   /* Check for enough free space, but only if we are actually going to
      download */
   if (_config->FindB("APT::Get::Print-URIs") == false &&
       _config->FindB("APT::Get::Download",true) == true)
   {
      struct statvfs Buf;
      string OutputDir = _config->FindDir("Dir::Cache::Archives");
      if (statvfs(OutputDir.c_str(),&Buf) != 0)
	 return _error->Errno("statvfs","Couldn't determine free space in %s",
			      OutputDir.c_str());
      // CNC:2002-07-11
      if (unsigned(Buf.f_bavail) < (FetchBytes - FetchPBytes)/Buf.f_bsize)
	 return _error->Error(_("You don't have enough free space in %s."),
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

      const char *Prompt = _("Yes, do as I say!");
      ioprintf(c2out,
	       _("You are about to do something potentially harmful\n"
		 "To continue type in the phrase '%s'\n"
		 " ?] "),Prompt);
      c2out << flush;
      if (AnalPrompt(Prompt) == false)
      {
	 c2out << _("Abort.") << endl;
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
	    c3out << "apt-get:wait-yes-no:" << endl;
	    c2out << _("Do you want to continue? [Y/n] ") << flush;

	    if (YnPrompt() == false)
	    {
	       c2out << _("Abort.") << endl;
	       exit(1);
	    }
	 }
      }
   }

   // Just print out the uris and exit if the --print-uris flag was used
   if (_config->FindB("APT::Get::Print-URIs") == true)
   {
      if (_config->FindB("APT::Get::PrintLocalFile"))
      {
         struct stat stb;
         for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin(); I < Fetcher.ItemsEnd(); ++I)
            if (((*I)->Local) && !stat((*I)->DestFile.c_str(), &stb))
               cout << (*I)->DestFile << endl;
         return true;
      }
      pkgAcquire::UriIterator I = Fetcher.UriBegin();
      for (; I != Fetcher.UriEnd(); I++)
	 cout << '\'' << I->URI << "' " << flNotDir(I->Owner->DestFile) << ' ' <<
	       I->Owner->FileSize << ' ' << I->Owner->CheckType() << ':' << I->Owner->ExpectedHash() << endl;
      return true;
   }

// CNC:2004-03-07 - lock not taken in download mode in the first place
#if 0
   /* Unlock the dpkg lock if we are not going to be doing an install
      after. */
   if (_config->FindB("APT::Get::Download-Only",false) == true)
      _system->UnLock();
#endif

   // CNC:2003-02-24
   bool Ret = true;

   // Run it
   while (1)
   {
      bool Transient = false;
      if (_config->FindB("APT::Get::Download",true) == false)
      {
	 for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin(); I < Fetcher.ItemsEnd();)
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

      // CNC:2003-02-24
      _error->PopState();

      // Print out errors
      bool Failed = false;
      for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
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

	 fprintf(stderr,_("Failed to fetch %s  %s\n"),(*I)->DescURI().c_str(),
		 (*I)->ErrorText.c_str());
	 Failed = true;
      }

      /* If we are in no download mode and missing files and there were
         'failures' then the user must specify -m. Furthermore, there
         is no such thing as a transient error in no-download mode! */
      if (Transient == true &&
	  _config->FindB("APT::Get::Download",true) == false)
      {
	 Transient = false;
	 Failed = true;
      }

      if (_config->FindB("APT::Get::Download-Only",false) == true)
      {
	 if (Failed == true && _config->FindB("APT::Get::Fix-Missing",false) == false)
	    return _error->Error(_("Some files failed to download"));
	 c1out << _("Download complete and in download only mode") << endl;
	 return true;
      }

      if (Failed == true && _config->FindB("APT::Get::Fix-Missing",false) == false)
      {
	 return _error->Error(_("Unable to fetch some archives, maybe run apt-get update or try with --fix-missing?"));
      }

      if (Transient == true && Failed == true)
	 return _error->Error(_("--fix-missing and media swapping is not currently supported"));

      // Try to deal with missing package files
      if (Failed == true && PM->FixMissing() == false)
      {
	 cerr << _("Unable to correct missing packages.") << endl;
	 return _error->Error(_("Aborting Install."));
      }

      // CNC:2002-10-18
      if (Transient == false || _config->FindB("Acquire::CDROM::Copy-All", false) == false) {
	 if (Transient == true) {
	    // We must do that in a system independent way. */
	    _config->Set("RPM::Install-Options::", "--nodeps");
	 }
	 _system->UnLock();
	 pkgPackageManager::OrderResult Res = PM->DoInstall();
	 if (Res == pkgPackageManager::Failed || _error->PendingError() == true)
	 {
	    if (Transient == false)
	       return false;
	    Ret = false;
	 }

	 // CNC:2002-07-06
	 if (Res == pkgPackageManager::Completed)
	 {
	    CommandLine *CmdL = NULL; // Watch out! If used will blow up!
	    if (_config->FindB("APT::Post-Install::Clean",false) == true)
	       Ret &= DoClean(*CmdL);
	    else if (_config->FindB("APT::Post-Install::AutoClean",false) == true)
	       Ret &= DoAutoClean(*CmdL);
	    return Ret;
	 }

	 _system->Lock();
      }

      // CNC:2003-02-24
      _error->PushState();

      // Reload the fetcher object and loop again for media swapping
      Fetcher.Shutdown();
      if (PM->GetArchives(&Fetcher,&List,&Recs) == false)
	 return false;
   }
}
									/*}}}*/
// CNC:2003-12-02
// DownloadPackages - Fetch packages					/*{{{*/
// ---------------------------------------------------------------------
/* Fetch packages */
bool DownloadPackages(vector<string> &URLLst)
{

   // Create the download object
   AcqTextStatus Stat(ScreenWidth,_config->FindI("quiet",0));
   pkgAcquire Fetcher(&Stat);

   // Load the requestd sources into the fetcher
   vector<string>::const_iterator I = URLLst.begin();
   for (; I != URLLst.end(); I++)
      new pkgAcqFile(&Fetcher,*I,"",0,*I,flNotDir(*I));

   // Run it
   if (Fetcher.Run() == pkgAcquire::Failed)
      return false;

   // Print error messages
   bool Failed = false;
   for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
   {
      if ((*I)->Status == pkgAcquire::Item::StatDone &&
	  (*I)->Complete == true)
	 continue;

      fprintf(stderr,_("Failed to fetch %s  %s\n"),(*I)->DescURI().c_str(),
	      (*I)->ErrorText.c_str());
      Failed = true;
   }
   if (Failed == true)
      return _error->Error(_("Failed to fetch some archives."));

   return true;
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
   // CNC:2004-03-03 - Improved virtual package handling.
   if (Pkg->VersionList == 0 && Pkg->ProvidesList != 0)
   {
      vector<pkgCache::Package *> GoodSolutions;
      unsigned long Size = 0;
      for (pkgCache::PrvIterator Prv = Pkg.ProvidesList();
	   Prv.end() == false; Prv++)
	 Size++;
      const SPtrArray<pkgCache::Package *> PList(new pkgCache::Package *[Size]);
      pkgCache::Package **PEnd = PList.get();
      for (pkgCache::PrvIterator Prv = Pkg.ProvidesList(); Prv.end() == false; Prv++)
         *PEnd++ = Prv.OwnerPkg();
      Fix.MakeScores();
      qsort(PList.get(),PEnd - PList.get(),sizeof(PList[0]),&(Fix.ScoreSort));

      for (unsigned int p=0; p<Size; ++p)
      {
         bool instVirtual = _config->FindB("APT::Install::Virtual", false);
         pkgCache::PkgIterator PrvPkg = pkgCache::PkgIterator(*Pkg.Cache(), PList[p]);
         pkgCache::PrvIterator Prv = Pkg.ProvidesList();
         for (; Prv.end() == false && Prv.OwnerPkg() != PrvPkg; Prv++)
            ;
	 // Check if it's a different version of a package already
	 // considered as a good solution.
	 bool AlreadySeen = false;
	 for (unsigned int i = 0; i != GoodSolutions.size(); i++)
	 {
	    pkgCache::PkgIterator GoodPkg(Cache, GoodSolutions[i]);
	    if (PrvPkg == GoodPkg)
	    {
	       AlreadySeen = true;
	       break;
	    }
	 }
	 if (AlreadySeen)
	    continue;
	 // Is the current version the provides owner?
	 if (PrvPkg.CurrentVer() == Prv.OwnerVer())
	 {
	    // Already installed packages are good solutions, since
	    // the user might try to install something he already has
	    // without being aware.
	    GoodSolutions.push_back(PrvPkg);
	    if (instVirtual)
		break;
	    continue;
	 }
	 pkgCache::VerIterator PrvPkgCandVer =
				 Cache[PrvPkg].CandidateVerIter(Cache);
	 if (PrvPkgCandVer.end() == true)
	 {
	    // Packages without a candidate version are not good solutions.
	    continue;
	 }
	 // Is the provides pointing to the candidate version?
	 bool good = false;
	 for (; PrvPkgCandVer.end() == false; ++PrvPkgCandVer)
	 {
	    if (PrvPkgCandVer == Prv.OwnerVer())
	    {
	       // Yes, it is. This is a good solution.
	       good = true;
	       GoodSolutions.push_back(PrvPkg);
	    }
	 }
	 if (good && instVirtual)
	    break;
      }
      vector<string> GoodSolutionNames;
      unsigned int GoodSolutionsInstalled = 0;
      for (unsigned int i = 0; i != GoodSolutions.size(); i++)
      {
	 pkgCache::PkgIterator GoodPkg(Cache, GoodSolutions[i]);
	 GoodSolutionNames.push_back(GoodPkg.Name());
	 if (GoodPkg.CurrentVer().end() == false)
	 {
	    GoodSolutionsInstalled++;
	 }
      }
#ifdef WITH_LUA
      if (GoodSolutions.size() > 1 && !(Remove && GoodSolutionsInstalled == 1))
      {
	 vector<string> VS;
	 _lua->SetDepCache(&Cache);
	 _lua->SetDontFix();
	 _lua->SetGlobal("virtualname", Pkg.Name());
	 _lua->SetGlobal("packages", GoodSolutions);
	 _lua->SetGlobal("packagenames", GoodSolutionNames);
	 _lua->SetGlobal("selected");
	 _lua->RunScripts("Scripts::AptGet::Install::SelectPackage");
	 pkgCache::Package *selected = _lua->GetGlobalPkg("selected");
         if (selected)
         {
	    GoodSolutions.clear();
	    GoodSolutions.push_back(selected);
	 }
	 else
	 {
	    vector<string> Tmp = _lua->GetGlobalStrList("packagenames");
	    if (Tmp.size() == GoodSolutions.size())
	       GoodSolutionNames = Tmp;
	 }
	 _lua->ResetGlobals();
	 _lua->ResetCaches();
      }
#endif
      if (GoodSolutions.size() == 1 ||
          ((GoodSolutions.size() > 1) && Remove && GoodSolutionsInstalled == 1))
      {
	 pkgCache::PkgIterator GoodPkg(Cache, GoodSolutions[0]);
	 ioprintf(c1out,_("Selecting %s for '%s'\n"),
		  GoodPkg.Name(), Pkg.Name());
	 Pkg = GoodPkg;
      }
      else if (GoodSolutions.size() == 0)
      {
	 _error->Error(_("Package %s is a virtual package with no "
			 "good providers.\n"), Pkg.Name());
	 return false;
      }
      else
      {
	 ioprintf(cerr,_("Package %s is a virtual package provided by:\n"),
		  Pkg.Name());
	 for (unsigned int i = 0; i != GoodSolutions.size(); i++)
	 {
	    pkgCache::PkgIterator GoodPkg(Cache, GoodSolutions[i]);
	    if (GoodPkg.CurrentVer().end() == false)
	       cerr  << "  " << GoodSolutionNames[i]
		     << " "  << Cache[GoodPkg].CandVersion
		     << _(" [Installed]") << endl;
	    else
	       cerr  << "  " << GoodSolutionNames[i]
		     << " "  << Cache[GoodPkg].CandVersion << endl;
	 }
	 if (Remove)
	   cerr << _("You should explicitly select one to remove.") << endl;
	 else
	   cerr << _("You should explicitly select one to install.") << endl;
	 _error->Error(_("Package %s is a virtual package with multiple "
			 "good providers.\n"), Pkg.Name());
	 return false;
      }
   }

   // Handle the no-upgrade case
   if (_config->FindB("APT::Get::upgrade",true) == false &&
       Pkg->CurrentVer != 0)
   {
      if (AllowFail == true)
	 ioprintf(c1out,_("Skipping %s, it is already installed and upgrade is not set.\n"),
		  Pkg.Name());
      return true;
   }

   // Check if there is something at all to install
   pkgDepCache::StateCache &State = Cache[Pkg];
   if (Remove == true && Pkg->CurrentVer == 0)
   {
      Fix.Clear(Pkg);
      Fix.Protect(Pkg);
      Fix.Remove(Pkg);

      /* We want to continue searching for regex hits, so we return false here
         otherwise this is not really an error. */
      if (AllowFail == false)
	 return false;

      ioprintf(c1out,_("Package %s is not installed, so not removed\n"),Pkg.Name());
      return true;
   }

   if (State.CandidateVer == 0 && Remove == false)
   {
      if (AllowFail == false)
	 return false;

// CNC:2004-03-03 - Improved virtual package handling.
#if 0
      if (Pkg->ProvidesList != 0)
      {
	 ioprintf(c1out,_("Package %s is a virtual package provided by:\n"),
		  Pkg.Name());

	 pkgCache::PrvIterator I = Pkg.ProvidesList();
	 for (; I.end() == false; I++)
	 {
	    pkgCache::PkgIterator Pkg = I.OwnerPkg();

	    if (Cache[Pkg].CandidateVerIter(Cache) == I.OwnerVer())
	    {
	       if (Cache[Pkg].Install() == true && Cache[Pkg].NewInstall() == false)
		  c1out << "  " << Pkg.Name() << " " << I.OwnerVer().VerStr() <<
		  _(" [Installed]") << endl;
	       else
		  c1out << "  " << Pkg.Name() << " " << I.OwnerVer().VerStr() << endl;
	    }
	 }
	 c1out << _("You should explicitly select one to install.") << endl;
      }
      else
#endif
      {
	 ioprintf(c1out,
	 _("Package %s has no available version, but exists in the database.\n"
	   "This typically means that the package was mentioned in a dependency and\n"
	   "never uploaded, has been obsoleted or is not available with the contents\n"
	   "of sources.list\n"),Pkg.Name());

	 string List;
	 string VersionsList;
         // allocate and zero memory
         const SPtrArray<bool> Seen(new bool[Cache.Head().PackageCount]());
	 pkgCache::DepIterator Dep = Pkg.RevDependsList();
	 for (; Dep.end() == false; Dep++)
	 {
	    // CNC:2002-07-30
	    if (Dep->Type != pkgCache::Dep::Replaces &&
	        Dep->Type != pkgCache::Dep::Obsoletes)
	       continue;
	    if (Seen[Dep.ParentPkg()->ID] == true)
	       continue;
	    Seen[Dep.ParentPkg()->ID] = true;
	    List += string(Dep.ParentPkg().Name()) + " ";
            //VersionsList += string(Dep.ParentPkg().CurVersion) + "\n"; ???
	 }
	 if (!List.empty()) c3out<<"apt-get:however-replace-list:"<<List<<endl;
	 ShowList(c1out,_("However the following packages replace it:"),List,VersionsList,ScreenWidth);
      }

      _error->Error(_("Package %s has no installation candidate"),Pkg.Name());
      return false;
   }

   Fix.Clear(Pkg);
   Fix.Protect(Pkg);
   if (Remove == true)
   {
      Fix.Remove(Pkg);
      Cache.MarkDelete(Pkg,_config->FindB("APT::Get::Purge",false));
      return true;
   }

   // Install it
   Cache.MarkInstall(Pkg,pkgDepCache::AutoMarkFlag::Manual,false);
   if (State.Install() == false)
   {
      if (_config->FindB("APT::Get::ReInstall",false) == true)
      {
	 if (Pkg->CurrentVer == 0)
	    ioprintf(c1out,_("Reinstallation of %s is not possible.\n"),
		     Pkg.Name());
	 else if (Pkg.CurrentVer().Downloadable() == false)
	    ioprintf(c1out,_("Reinstallation of %s %s is not possible, it cannot be downloaded.\n"),
		     Pkg.Name(), Pkg.CurrentVer().VerStr());
	 else
	    Cache.SetReInstall(Pkg,true);
      }
      else
      {
	 if (AllowFail == true)
	    ioprintf(c1out,_("%s is already the newest version.\n"),
		     Pkg.Name());
	    // FIXME: should we set it in this case?
	    // under if: Cache.MarkAuto(Pkg, pkgDepCache::AutoMarkFlag::Manual);
	    // Currently, even if it's set, unless package is reinstalled, new mark is not saved
      }
   }
   else
   {
      ExpectedInst++;
   }

   // Install it with autoinstalling enabled.
   if (State.InstBroken() == true && BrokenFix == false)
      Cache.MarkInstall(Pkg,pkgDepCache::AutoMarkFlag::DontChange,true);
   return true;
}
									/*}}}*/
// TryToChangeVer - Try to change a candidate version			/*{{{*/
// ---------------------------------------------------------------------
/* */
// CNC:2003-11-05 - Applied patch by ALT-Linux changing the way
//                  versions are requested by the user.
static const char *op2str(int op)
{
   switch (op & 0x0f)
   {
      case pkgCache::Dep::LessEq: return "<=";
      case pkgCache::Dep::GreaterEq: return ">=";
      case pkgCache::Dep::Less: return "<";
      case pkgCache::Dep::Greater: return ">";
      case pkgCache::Dep::Equals: return "=";
      case pkgCache::Dep::NotEquals: return "!";
      default: return "";
   }
}

// best versions go first
class bestVersionOrder
{
   private:
      pkgDepCache &Cache_;
      pkgProblemResolver &Fix_;
   public:
      bestVersionOrder(pkgDepCache &Cache, pkgProblemResolver &Fix)
	 : Cache_(Cache), Fix_(Fix) { }
      bool operator() (const pkgCache::VerIterator &a, const pkgCache::VerIterator &b)
      {
	 // CmpVersion sorts ascending
	 int cmp = Cache_.VS().CmpVersion(a.VerStr(), b.VerStr());
	 if (cmp == 0)
	 {
	    const pkgCache::Package *A = &(*a.ParentPkg());
	    const pkgCache::Package *B = &(*b.ParentPkg());
	    // ScoreSort sorts descending
	    cmp = Fix_.ScoreSort(&B, &A);
	 }
	 //fprintf(stderr, "%s %s <=> %s %s = %d\n",
	 //      a.ParentPkg().Name(), a.VerStr(),
	 //      b.ParentPkg().Name(), b.VerStr(),
	 //      cmp);
	 return cmp > 0;
      }
};

static void __attribute__((unused))
printVerList(const char *msg, const std::list<pkgCache::VerIterator> &list)
{
   std::list<pkgCache::VerIterator>::const_iterator I = list.begin();
   for ( ; I != list.end(); ++I)
   {
      if (I == list.begin())
	 fprintf(stderr, "%s: ", msg);
      else
	 fprintf(stderr, ", ");

      const pkgCache::VerIterator &Ver = *I;
      fprintf(stderr, "%s#%s", Ver.ParentPkg().Name(), Ver.VerStr());
   }
   fprintf(stderr, "\n");
}

// CNC:2003-11-11
bool TryToChangeVer(pkgCache::PkgIterator &Pkg,pkgDepCache &Cache,
		    pkgProblemResolver &Fix,
		    int VerOp,const char *VerTag,bool IsRel)
{
   // CNC:2003-11-05
   pkgVersionMatch Match(VerTag,(IsRel == true?pkgVersionMatch::Release :
				 pkgVersionMatch::Version),VerOp);

   std::list<pkgCache::VerIterator> found = Match.FindAll(Pkg);
   //printVerList("found", found);

   if (found.size() == 0)
   {
      // CNC:2003-11-05
      if (IsRel == true)
	 return _error->Error(_("Release %s'%s' for '%s' was not found"),
			      op2str(VerOp),VerTag,Pkg.Name());
      return _error->Error(_("Version %s'%s' for '%s' was not found"),
			   op2str(VerOp),VerTag,Pkg.Name());
   }

   if (found.size() > 1)
   {
      Fix.MakeScores();
      bestVersionOrder order(Cache,Fix);
      found.sort(order);
      found.unique();
      //printVerList("sorted", found);
   }

   pkgCache::VerIterator Ver = found.front();
   int already = 0;

   std::list<pkgCache::VerIterator>::const_iterator I = found.begin();
   for ( ; I != found.end(); ++I)
   {
      const pkgCache::VerIterator &V = *I;
      if (V.ParentPkg().CurrentVer() == V)
      {
	 Ver = V;
	 already = 2;
	 break;
      }
      if (Cache[V.ParentPkg()].InstallVer == V.operator const pkgCache::Version *())
      {
	 Ver = V;
	 already = 1;
	 break;
      }
   }

   if (strcmp(VerTag,Ver.VerStr()) != 0 || found.size() > 1)
   {
      // CNC:2003-11-11
      const char *fmt;
      if (IsRel == true)
      {
	 if (already > 1)
	    fmt = _("Version %s#%s (%s) for %s%s%s is already installed\n");
	 else if (already)
	    fmt = _("Version %s#%s (%s) for %s%s%s is already selected for install\n");
	 else
	    fmt = _("Selected version %s#%s (%s) for %s%s%s\n");

	 ioprintf(c1out,fmt,
		  Ver.ParentPkg().Name(),Ver.VerStr(),Ver.RelStr().c_str(),
		  Pkg.Name(),op2str(VerOp),VerTag);
      }
      else
      {
	 if (already > 1)
	    fmt = _("Version %s#%s for %s%s%s is already installed\n");
	 else if (already)
	    fmt = _("Version %s#%s for %s%s%s is already selected for install\n");
	 else
	    fmt = _("Selected version %s#%s for %s%s%s\n");

	 ioprintf(c1out,fmt,
		  Ver.ParentPkg().Name(),Ver.VerStr(),
		  Pkg.Name(),op2str(VerOp),VerTag);
      }
   }

   Cache.SetCandidateVersion(Ver);
   // CNC:2003-11-11
   Pkg = Ver.ParentPkg();
   return true;
}
									/*}}}*/
// FindSrc - Find a source record					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgSrcRecords::Parser *FindSrc(const char *Name,pkgRecords &Recs,
			       pkgSrcRecords &SrcRecs,string &Src,
			       pkgDepCache &Cache)
{
   // We want to pull the version off the package specification..
   string VerTag;
   string TmpSrc = Name;
   string::size_type Slash = TmpSrc.rfind('=');
   if (Slash != string::npos)
   {
      VerTag = string(TmpSrc.begin() + Slash + 1,TmpSrc.end());
      TmpSrc = string(TmpSrc.begin(),TmpSrc.begin() + Slash);
   }

   /* Lookup the version of the package we would install if we were to
      install a version and determine the source package name, then look
      in the archive for a source package of the same name. In theory
      we could stash the version string as well and match that too but
      today there aren't multi source versions in the archive. */
   if (_config->FindB("APT::Get::Only-Source") == false &&
       VerTag.empty() == true)
   {
      pkgCache::PkgIterator Pkg = Cache.FindPkg(TmpSrc);
      if (Pkg.end() == false)
      {
	 pkgCache::VerIterator Ver = Cache.GetCandidateVer(Pkg);
	 if (Ver.end() == false)
	 {
	    pkgRecords::Parser &Parse = Recs.Lookup(Ver.FileList());
	    Src = Parse.SourcePkg();
	 }
      }
   }

   // No source package name..
   if (Src.empty() == true)
      Src = TmpSrc;

   // The best hit
   pkgSrcRecords::Parser *Last = 0;
   unsigned long Offset = 0;
   string Version;
   bool IsMatch = false;

   // If we are matching by version then we need exact matches to be happy
   if (VerTag.empty() == false)
      IsMatch = true;

   /* Iterate over all of the hits, which includes the resulting
      binary packages in the search */
   pkgSrcRecords::Parser *Parse;
   SrcRecs.Restart();
   while ((Parse = SrcRecs.Find(Src.c_str(),false)) != 0)
   {
      string Ver = Parse->Version();

      // Skip name mismatches
      if (IsMatch == true && Parse->Package() != Src)
	 continue;

      if (VerTag.empty() == false)
      {
	 /* Don't want to fall through because we are doing exact version
	    matching. */
	 if (Cache.VS().CmpVersion(VerTag,Ver) != 0)
	    continue;

	 Last = Parse;
	 Offset = Parse->Offset();
	 break;
      }

      // Newer version or an exact match
      if (Last == 0 || Cache.VS().CmpVersion(Version,Ver) < 0 ||
	  (Parse->Package() == Src && IsMatch == false))
      {
	 IsMatch = Parse->Package() == Src;
	 Last = Parse;
	 Offset = Parse->Offset();
	 Version = Ver;
      }
   }

   if (Last == 0)
      return 0;

   if (Last->Jump(Offset) == false)
      return 0;

   return Last;
}
									/*}}}*/

// DoUpdate - Update the package lists					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool DoUpdate(CommandLine &CmdL)
{
   pkgSourceList List;

   if (CmdL.FileSize() != 1)
   {
      List.ReadVendors();
      for (const char **I = CmdL.FileList + 1; *I != 0; I++)
      {
	 string Repo = _config->FindDir("Dir::Etc::sourceparts") + *I;
	 if (FileExists(Repo) == false)
	    Repo += ".list";
	 if (FileExists(Repo) == true)
	 {
	    if (!List.ReadAppend(Repo))
	       return _error->Error(_("Sources list %s could not be read"),Repo.c_str());
	 }
	 else
	    return _error->Error(_("Sources list %s doesn't exist"),Repo.c_str());
      }
   }
   else if (List.ReadMainList() == false)
      return false;

   // Just print out the uris and exit if the --print-uris flag was used
   if (_config->FindB("APT::Get::Print-URIs") == true)
   {
      // Create the download object
      AcqTextStatus Stat(ScreenWidth,_config->FindI("quiet",0));
      pkgAcquire Fetcher(&Stat);

      // Populate it with release file URIs
      // InvalidateReleases() isn't really necessary, since apt-get is
      // a one-shot process. It's here for consistency (with other utilities).
      if (! (List.InvalidateReleases() && List.GetReleases(&Fetcher)) )
         return false;

      // Populate it with the source selection
      if (!List.GetIndexes(&Fetcher))
         return false;

      if (_config->FindB("APT::Get::PrintLocalFile"))
      {
         struct stat stb;
         for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); ++I)
            if (((*I)->Local) && !stat((*I)->DestFile.c_str(), &stb))
               cout << (*I)->DestFile << endl;
      }
      else
      {
         for (pkgAcquire::UriIterator I = Fetcher.UriBegin(); I != Fetcher.UriEnd(); ++I)
            cout << '\'' << I->URI << "' " << flNotDir(I->Owner->DestFile) << ' ' <<
               I->Owner->FileSize << ' ' << I->Owner->CheckType() << ':' << I->Owner->ExpectedHash() << endl;
      }

      return true;
   }

   // do the work

   // Needed for the download object
   AcqTextStatus Stat(ScreenWidth,_config->FindI("quiet",0));
   // Prepare the cache.
   CacheFile Cache(c1out, true /* WithLock */);

   if (!ListUpdate(Stat, List, Cache))
      return false;

   if (Cache.Open() == false)
      return false;

   return true;
}
									/*}}}*/
// DoUpgrade - Upgrade all packages					/*{{{*/
// ---------------------------------------------------------------------
/* Upgrade all packages without installing new packages or erasing old
   packages */
bool DoUpgrade(CommandLine &CmdL)
{
   if (!_config->FindB("APT::Get::EnableUpgrade", false)) {
      return _error->Error(_("'apt-get upgrade' is disabled because it can leave system in a broken state.\n"
                             "It is advised to use 'apt-get dist-upgrade' instead.\n"
                             "If you still want to use 'apt-get upgrade' despite of this warning,\n"
                             "use '--enable-upgrade' option or enable 'APT::Get::EnableUpgrade' configuration setting"));
   }

   CacheFile Cache(c1out,ShouldLockForInstall());
   if (Cache.Open() == false || Cache.CheckDeps() == false)
      return false;

   // Do the upgrade
   if (pkgAllUpgrade(Cache) == false)
   {
      ShowBroken(cerr,Cache,false);
      return _error->Error(_("Internal Error, AllUpgrade broke stuff"));
   }

// CNC:2003-03-19
#ifdef WITH_LUA
   _lua->SetDepCache(Cache);
   _lua->RunScripts("Scripts::AptGet::Upgrade");
   _lua->ResetCaches();
#endif

   // CNC:2003-03-06
   if (CheckOnly(Cache) == true)
      return true;

   return InstallPackages(Cache,true);
}
									/*}}}*/
// DoInstall - Install packages from the command line			/*{{{*/
// ---------------------------------------------------------------------
/* Install named packages */
bool DoInstall(CommandLine &CmdL)
{
   CacheFile Cache(c1out,ShouldLockForInstall());
   if (Cache.Open() == false ||
       Cache.CheckDeps(CmdL.FileSize() != 1) == false)
      return false;

   // Enter the special broken fixing mode if the user specified arguments
   bool BrokenFix = false;
   if (Cache->BrokenCount() != 0)
      BrokenFix = true;

   unsigned int ExpectedInst = 0;
   unsigned int Packages = 0;
   pkgProblemResolver Fix(Cache);

   bool doAutoRemove = _config->FindB("APT::Get::AutomaticRemove", false);

   bool DefRemove = false;
   if (strcasecmp(CmdL.FileList[0],"remove") == 0)
      DefRemove = true;
   // CNC:2004-03-22
   else if (strcasecmp(CmdL.FileList[0],"reinstall") == 0)
      _config->Set("APT::Get::ReInstall", true);

   for (const char **I = CmdL.FileList + 1; *I != 0; I++)
   {
      // Duplicate the string
      unsigned int Length = strlen(*I);
      char S[300];
      if (Length >= sizeof(S))
	 continue;
      strcpy(S,*I);

      // CNC:2003-03-15
      char OrigS[300];
      strcpy(OrigS,S);

      // See if we are removing and special indicators..
      bool Remove = DefRemove;
      char *VerTag = 0;
      bool VerIsRel = false;
      // CNC:2003-11-05
      int VerOp = 0;
      while (Cache->FindPkg(S).end() == true)
      {
	 // Handle an optional end tag indicating what to do
	 if (Length >= 1 && S[Length - 1] == '-')
	 {
	    Remove = true;
	    S[--Length] = 0;
	    continue;
	 }

	 if (Length >= 1 && S[Length - 1] == '+')
	 {
	    Remove = false;
	    S[--Length] = 0;
	    continue;
	 }

	 // CNC:2003-11-05
	 char *sep = strpbrk(S,"=><");
	 if (sep)
	 {
	    char *p;
	    int eq = 0, gt = 0, lt = 0;

	    VerIsRel = false;
	    for (p = sep; *p && strchr("=><",*p); ++p)
	       switch (*p)
	       {
		  case '=': eq = 1; break;
		  case '>': gt = 1; break;
		  case '<': lt = 1; break;
	       }
	    if (eq)
	    {
	       if (lt && gt)
		  return _error->Error(_("Couldn't parse name '%s'"),S);
	       else if (lt)
		  VerOp = pkgCache::Dep::LessEq;
	       else if (gt)
		  VerOp = pkgCache::Dep::GreaterEq;
	       else
		  VerOp = pkgCache::Dep::Equals;
	    }
	    else
	    {
	       if (lt && gt)
		  VerOp = pkgCache::Dep::NotEquals;
	       else if (lt)
		  VerOp = pkgCache::Dep::Less;
	       else if (gt)
		  VerOp = pkgCache::Dep::Greater;
	       else
		  return _error->Error(_("Couldn't parse name '%s'"),S);
	    }
	    *sep = '\0';
	    /* S may be overwritten later, for example, if it contains
	     * a file name that will be resolved to a package.
	     * So we point VerTag to the same offset in OrigS. */
	    VerTag = (p - S) + OrigS;
	 }

	 // CNC:2003-11-21 - Try to handle unknown file items.
	 if (S[0] == '/')
	 {
	    pkgRecords Recs(Cache);
	    if (_error->PendingError() == true)
	       return false;
	    pkgCache::PkgIterator Pkg = (*Cache).PkgBegin();
	    for (; Pkg.end() == false; Pkg++)
	    {
	       // Should we try on all versions?
	       pkgCache::VerIterator Ver = (*Cache)[Pkg].CandidateVerIter(*Cache);
	       if (Ver.end() == false)
	       {
		  pkgRecords::Parser &Parse = Recs.Lookup(Ver.FileList());
		  if (Parse.HasFile(S)) {
		     strcpy(S, Pkg.Name());
		     ioprintf(c1out,_("Selecting %s for '%s'\n"),
			      Pkg.Name(),OrigS);
		     // Confirm the translation.
		     ExpectedInst += 1000;
		     break;
		  }
	       }
	    }
	 }

	 char *Slash = strchr(S,'/');
	 if (Slash != 0)
	 {
	    VerIsRel = true;
	    *Slash = 0;
	    VerTag = Slash + 1;
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
	    if (*I == '?' || *I == '*' || *I == '|' ||
	        *I == '[' || *I == '^' || *I == '$')
	       break;

	 // CNC:2003-05-15
	 if (*I == 0) {
#ifdef WITH_LUA
	    vector<string> VS;
	    _lua->SetDepCache(Cache);
	    _lua->SetDontFix();
	    _lua->SetGlobal("argument", OrigS);
	    _lua->SetGlobal("translated", VS);
	    _lua->RunScripts("Scripts::AptGet::Install::TranslateArg");
	    const char *name = _lua->GetGlobalStr("translated");
	    if (name != NULL) {
	       VS.push_back(name);
	    } else {
	       VS = _lua->GetGlobalStrList("translated");
	    }
	    _lua->ResetGlobals();
	    _lua->ResetCaches();

	    // Translations must always be confirmed
	    ExpectedInst += 1000;

	    // Run over the matches
	    bool Hit = false;
	    for (vector<string>::const_iterator I = VS.begin();
	         I != VS.end(); I++) {

	       Pkg = Cache->FindPkg(*I);
	       if (Pkg.end() == true)
		  continue;

	       ioprintf(c1out,_("Selecting %s for '%s'\n"),
			Pkg.Name(),OrigS);

	       Hit |= TryToInstall(Pkg,Cache,Fix,Remove,BrokenFix,
				   ExpectedInst,true);
	    }

	    if (Hit == true)
	       continue;
#endif
	    return _error->Error(_("Couldn't find package %s"), OrigS);
	 }

	 // Regexs must always be confirmed
	 ExpectedInst += 1000;

	 // Compile the regex pattern
	 regex_t Pattern;
	 int Res;
	 if ((Res = regcomp(&Pattern,S,REG_EXTENDED | REG_ICASE |
		     REG_NOSUB)) != 0)
	 {
	    char Error[300];
	    regerror(Res,&Pattern,Error,sizeof(Error));
	    return _error->Error(_("Regex compilation error - %s"),Error);
	 }

	 // Run over the matches
	 bool Hit = false;
	 for (Pkg = Cache->PkgBegin(); Pkg.end() == false; Pkg++)
	 {
	    if (regexec(&Pattern,Pkg.Name(),0,0,0) != 0)
	       continue;

	    // CNC:2003-11-23
	    ioprintf(c1out,_("Selecting %s for '%s'\n"),
		     Pkg.Name(), OrigS);

	    if (VerTag != 0)
	       // CNC:2003-11-05
	       if (TryToChangeVer(Pkg,Cache,Fix,VerOp,VerTag,VerIsRel) == false)
		  return false;

	    Hit |= TryToInstall(Pkg,Cache,Fix,Remove,BrokenFix,
				ExpectedInst,false);
	 }
	 regfree(&Pattern);

	 if (Hit == false)
	    return _error->Error(_("Couldn't find package %s"), OrigS);
      }
      else
      {
	 if (VerTag != 0)
	    // CNC:2003-11-05
	    if (TryToChangeVer(Pkg,Cache,Fix,VerOp,VerTag,VerIsRel) == false)
	       return false;
	 if (TryToInstall(Pkg,Cache,Fix,Remove,BrokenFix,ExpectedInst) == false)
	    return false;
      }
   }

   if (doAutoRemove)
   {
      std::set<std::string> removed_packages;
      if (not pkgAutoremoveGetKeptAndUnneededPackages(*Cache, nullptr, &removed_packages))
      {
         return _error->Error(_("Requested autoremove failed."));
      }

      for (pkgCache::PkgIterator pkg = Cache->PkgBegin(); ! pkg.end(); ++pkg)
      {
         if (removed_packages.count(pkg.Name()) != 0)
         {
            Fix.Clear(pkg);
            Fix.Protect(pkg);
            Fix.Remove(pkg);
         }
      }
   }

// CNC:2003-03-19
#ifdef WITH_LUA
   _lua->SetDepCache(Cache);
   _lua->SetDontFix();
   _lua->RunScripts("Scripts::AptGet::Install::PreResolve");
   _lua->ResetCaches();
#endif

   // CNC:2002-08-01
   if (_config->FindB("APT::Remove-Depends",false) == true)
      Fix.RemoveDepends();

   /* If we are in the Broken fixing mode we do not attempt to fix the
      problems. This is if the user invoked install without -f and gave
      packages */
   if (BrokenFix == true && Cache->BrokenCount() != 0)
   {
      c1out << _("You might want to run `apt-get --fix-broken install' to correct these:") << endl;
      ShowBroken(cerr,Cache,false);

      return _error->Error(_("Unmet dependencies. Try 'apt-get --fix-broken install' with no packages (or specify a solution)."));
   }

   // Call the scored problem resolver
   Fix.InstallProtect();
   if (Fix.Resolve(true) == false)
      _error->Discard();

// CNC:2003-03-19
#ifdef WITH_LUA
   if (Cache->BrokenCount() == 0) {
      _lua->SetDepCache(Cache);
      _lua->SetProblemResolver(&Fix);
      _lua->RunScripts("Scripts::AptGet::Install::PostResolve");
      _lua->ResetCaches();
   }
#endif

   // Now we check the state of the packages,
   if (Cache->BrokenCount() != 0)
   {
      c1out <<
       _("Some packages could not be installed. This may mean that you have\n"
	 "requested an impossible situation or if you are using the unstable\n"
	 "distribution that some required packages have not yet been created\n"
	 "or been moved out of Incoming.") << endl;
      if (Packages == 1)
      {
	 c1out << endl;
	 c1out <<
	  _("Since you only requested a single operation it is extremely likely that\n"
	    "the package is simply not installable and a bug report against\n"
	    "that package should be filed.") << endl;
      }

      c1out << _("The following information may help to resolve the situation:") << endl;
      c1out << endl;
      ShowBroken(cerr,Cache,false);
      return _error->Error(_("Broken packages"));
   }

   /* Print out a list of packages that are going to be installed extra
      to what the user asked */
   if (Cache->InstCount() != ExpectedInst)
   {
      string List;
      string VersionsList;
      for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
      {
	 pkgCache::PkgIterator I(Cache,Cache.List[J]);
	 if ((*Cache)[I].Install() == false)
	    continue;

	 // CNC:2004-06-15
	 const char **K;
	 for (K = CmdL.FileList + 1; *K != 0; K++)
	    if (strcmp(*K,I.Name()) == 0)
		break;

	 if (*K == 0) {
	    List += string(I.Name()) + " ";
        VersionsList += string(Cache[I].CandVersion) + "\n";
     }
      }

      if (!List.empty()) c3out<<"apt-get:extra-list:"<<List<<endl;
      ShowList(c1out,_("The following extra packages will be installed:"),List,VersionsList,ScreenWidth);
   }

   /* Print out a list of suggested and recommended packages */
   {
      string SuggestsList, RecommendsList, List;
      string SuggestsVersions, RecommendsVersions;
      for (unsigned J = 0; J < Cache->Head().PackageCount; J++)
      {
	 pkgCache::PkgIterator I(Cache,Cache.List[J]);

	 /* Just look at the ones we want to install */
	 if ((*Cache)[I].Install() == false)
	   continue;

	 for (pkgCache::VerIterator V = I.VersionList(); V.end() == false; V++)
	   {
	     for (pkgCache::DepIterator D = V.DependsList(); D.end() == false; D++)
	       {
		 pkgCache::DepIterator Start;
		 pkgCache::DepIterator End;
		 D.GlobOr(Start,End);

		 /*
		  * If this is a virtual package, we need to check the list of
		  * packages that provide it and see if any of those are
		  * installed
		  */
		 pkgCache::PrvIterator Prv = Start.TargetPkg().ProvidesList();
		 bool providedBySomething = false;
		 for (; Prv.end() != true; Prv++)
		    if ((*Cache)[Prv.OwnerPkg()].InstVerIter(*Cache).end() == false) {
		       providedBySomething = true;
		       break;
		    }

		 if (providedBySomething) continue;

		 do
		   {
		     if (Start->Type == pkgCache::Dep::Suggests) {

		       /* A suggests relations, let's see if we have it
			  installed already */

		       string target = string(Start.TargetPkg().Name()) + " ";
		       if ((*Start.TargetPkg()).SelectedState == pkgCache::State::Install || Cache[Start.TargetPkg()].Install())
			 break;
		       /* Does another package suggest it as well?  If so,
			  don't print it twice */
		       if (int(SuggestsList.find(target)) > -1)
			 break;
		       SuggestsList += target;
		       SuggestsVersions += string(Cache[Start.TargetPkg()].CandVersion) + "\n";
		     }

		     if (Start->Type == pkgCache::Dep::Recommends) {

		       /* A recommends relation, let's see if we have it
			  installed already */

		       string target = string(Start.TargetPkg().Name()) + " ";
		       if ((*Start.TargetPkg()).SelectedState == pkgCache::State::Install || Cache[Start.TargetPkg()].Install())
			 break;

		       /* Does another package recommend it as well?  If so,
			  don't print it twice */

		       if (int(RecommendsList.find(target)) > -1)
			 break;
		       RecommendsList += target;
		       SuggestsVersions += string(Cache[Start.TargetPkg()].CandVersion) + "\n";
		     }
	      if (Start == End)
		break;
	      Start++;
	    } while (1);
	       }
	   }
      }
      if (!SuggestsList.empty()) c3out<<"apt-get:suggest-list:"<<SuggestsList<<std::endl;
      ShowList(c1out,_("Suggested packages:"),SuggestsList,SuggestsVersions,ScreenWidth);
      if (!RecommendsList.empty()) c3out<<"apt-get:recommended-list:"<<RecommendsList<<std::endl;
      ShowList(c1out,_("Recommended packages:"),RecommendsList,RecommendsVersions,ScreenWidth);

   }

   // CNC:2003-03-06
   if (CheckOnly(Cache) == true)
      return true;

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
   CacheFile Cache(c1out,ShouldLockForInstall());
   if (Cache.Open() == false || Cache.CheckDeps() == false)
      return false;

   c0out << _("Calculating Upgrade... ") << flush;
   if (pkgDistUpgrade(*Cache) == false)
   {
      c0out << _("Failed") << endl;
      ShowBroken(cerr,Cache,false);
      return false;
   }

// CNC:2003-03-19
#ifdef WITH_LUA
   _lua->SetDepCache(Cache);
   _lua->RunScripts("Scripts::AptGet::DistUpgrade");
   _lua->ResetCaches();
#endif

   // CNC:2003-03-06
   if (CheckOnly(Cache) == true)
      return true;

   c0out << _("Done") << endl;

   return InstallPackages(Cache,true);
}
									/*}}}*/
// DoDSelectUpgrade - Do an upgrade by following dselects selections	/*{{{*/
// ---------------------------------------------------------------------
/* Follows dselect's selections */
bool DoDSelectUpgrade(CommandLine &CmdL)
{
   CacheFile Cache(c1out,ShouldLockForInstall());
   if (Cache.Open() == false || Cache.CheckDeps() == false)
      return false;

   // Install everything with the install flag set
   pkgCache::PkgIterator I = Cache->PkgBegin();
   for (;I.end() != true; I++)
   {
      /* Install the package only if it is a new install, the autoupgrader
         will deal with the rest */
      if (I->SelectedState == pkgCache::State::Install)
	 Cache->MarkInstall(I,pkgDepCache::AutoMarkFlag::DontChange,false);
   }

   /* Now install their deps too, if we do this above then order of
      the status file is significant for | groups */
   for (I = Cache->PkgBegin();I.end() != true; I++)
   {
      /* Install the package only if it is a new install, the autoupgrader
         will deal with the rest */
      if (I->SelectedState == pkgCache::State::Install)
	 Cache->MarkInstall(I,pkgDepCache::AutoMarkFlag::DontChange,true);
   }

   // Apply erasures now, they override everything else.
   for (I = Cache->PkgBegin();I.end() != true; I++)
   {
      // Remove packages
      if (I->SelectedState == pkgCache::State::DeInstall ||
	  I->SelectedState == pkgCache::State::Purge)
	 Cache->MarkDelete(I,I->SelectedState == pkgCache::State::Purge);
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
	 ShowBroken(cerr,Cache,false);
	 return _error->Error("Internal Error, problem resolver broke stuff");
      }
   }

   // Now upgrade everything
   if (pkgAllUpgrade(Cache) == false)
   {
      ShowBroken(cerr,Cache,false);
      return _error->Error("Internal Error, problem resolver broke stuff");
   }

   // CNC:2003-03-06
   if (CheckOnly(Cache) == true)
      return true;

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
   virtual void Erase(const char *File,string Pkg,string Ver,struct stat &St) override
   {
      c1out << "Del " << Pkg << " " << Ver << " [" << SizeToStr(St.st_size) << "B]" << endl;

      if (_config->FindB("APT::Get::Simulate") == false)
         unlink(File);
   }
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

   CacheFile Cache(c1out, true /* WithLock */);
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
   CacheFile Cache(c1out, true /* WithLock */);
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
   CacheFile Cache(c1out, false /* not WithLock */);
   if (Cache.Open() == false)
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
      // CNC:2004-09-23 - Try to handle unknown file items.
      unsigned int Length = strlen(*I);
      char S[300];
      if (Length >= sizeof(S))
        continue;
      strcpy(S,*I);

      if (S[0] == '/')
      {
	 pkgRecords Recs(Cache);
	 if (_error->PendingError() == true)
	    return false;
	 pkgCache::PkgIterator Pkg = (*Cache).PkgBegin();
	 for (; Pkg.end() == false; Pkg++)
	 {
	    // Should we try on all versions?
	    pkgCache::VerIterator Ver = (*Cache)[Pkg].CandidateVerIter(*Cache);
	    if (Ver.end() == false)
	    {
	       pkgRecords::Parser &Parse = Recs.Lookup(Ver.FileList());
	       if (Parse.HasFile(S)) {
		  ioprintf(c1out,_("Selecting %s for '%s'\n"),
			   Pkg.Name(),S);
		  strcpy(S, Pkg.Name());
		  break;
	       }
	    }
	 }
      }

      string Src;
      pkgSrcRecords::Parser *Last = FindSrc(S,Recs,SrcRecs,Src,*Cache);

      if (Last == 0)
	 return _error->Error(_("Unable to find a source package for %s"),Src.c_str());

      // Back track
      vector<pkgSrcRecords::File> Lst;
      if (Last->Files(Lst) == false)
	 return false;

      // Load them into the fetcher
      for (vector<pkgSrcRecords::File>::const_iterator I = Lst.begin();
	   I != Lst.end(); I++)
      {
	 if (I->Type == "srpm") {
	    Dsc[J].Package = Last->Package();
	    Dsc[J].Version = Last->Version();
	    Dsc[J].Dsc = flNotDir(I->Path);
	 } else {
	    continue;
	 }

	if (_config->FindB("Debug::pkgAcquire::Auth",false) == true)
	    cerr << "I->Path = " << I->Path << ", I->MD5Hash = " << I->MD5Hash << endl;

	 new pkgAcqFile(&Fetcher,Last->Index().ArchiveURI(I->Path),
			I->MD5Hash,I->Size,
			Last->Index().SourceInfo(*Last,*I),Src);
      }
   }

   // Display statistics
   double FetchBytes = Fetcher.FetchNeeded();
   double FetchPBytes = Fetcher.PartialPresent();
   double DebBytes = Fetcher.TotalNeeded();

   // Check for enough free space
   struct statvfs Buf;
   string OutputDir = ".";
   if (statvfs(OutputDir.c_str(),&Buf) != 0)
      return _error->Errno("statvfs","Couldn't determine free space in %s",
			   OutputDir.c_str());
   // CNC:2002-07-12
   if (unsigned(Buf.f_bavail) < (FetchBytes - FetchPBytes)/Buf.f_bsize)
      return _error->Error(_("You don't have enough free space in %s"),
			   OutputDir.c_str());

   // Number of bytes
   if (DebBytes != FetchBytes)
      ioprintf(c1out,_("Need to get %sB/%sB of source archives.\n"),
	       SizeToStr(FetchBytes).c_str(),SizeToStr(DebBytes).c_str());
   else
      ioprintf(c1out,_("Need to get %sB of source archives.\n"),
	       SizeToStr(DebBytes).c_str());

   if (_config->FindB("APT::Get::Simulate",false) == true)
   {
      for (unsigned I = 0; I != J; I++)
	 ioprintf(cout,_("Fetch Source %s\n"),Dsc[I].Package.c_str());
      return true;
   }

   // Just print out the uris and exit if the --print-uris flag was used
   if (_config->FindB("APT::Get::Print-URIs") == true)
   {
      if (_config->FindB("APT::Get::PrintLocalFile"))
      {
         struct stat stb;
         for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin(); I < Fetcher.ItemsEnd(); ++I)
            if (((*I)->Local) && !stat((*I)->DestFile.c_str(), &stb))
               cout << (*I)->DestFile << endl;
         return true;
      }
      pkgAcquire::UriIterator I = Fetcher.UriBegin();
      for (; I != Fetcher.UriEnd(); I++)
	 cout << '\'' << I->URI << "' " << flNotDir(I->Owner->DestFile) << ' ' <<
	       I->Owner->FileSize << ' ' << I->Owner->CheckType() << ':' << I->Owner->ExpectedHash() << endl;
      return true;
   }

   // Run it
   if (Fetcher.Run() == pkgAcquire::Failed)
      return false;

   // Print error messages
   bool Failed = false;
   for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
   {
      if ((*I)->Status == pkgAcquire::Item::StatDone &&
	  (*I)->Complete == true)
	 continue;

      fprintf(stderr,_("Failed to fetch %s  %s\n"),(*I)->DescURI().c_str(),
	      (*I)->ErrorText.c_str());
      Failed = true;
   }
   if (Failed == true)
      return _error->Error(_("Failed to fetch some archives."));

   if (_config->FindB("APT::Get::Download-only",false) == true)
   {
      c1out << _("Download complete and in download only mode") << endl;
      return true;
   }

   // Unpack the sources
   pid_t Process = ExecFork();

   if (Process == 0)
   {
      for (unsigned I = 0; I != J; I++)
      {
	 string Dir = Dsc[I].Package + '-' + Cache->VS().UpstreamVersion(Dsc[I].Version.c_str());

	 if (Dsc[I].Dsc.empty() == true)
	    continue;

	 if (_config->FindB("APT::Get::Compile",false) == true)
	 {
	    char S[500];
	    snprintf(S,sizeof(S),"%s %s %s",
		     _config->Find("RPM::Source::Build-Command","rpm --rebuild").c_str(),
		     _config->Find("RPM::Source::Build-Options","").c_str(),
		     Dsc[I].Dsc.c_str());
	    if (system(S) != 0)
	    {
	       fprintf(stderr,_("Build command '%s' failed.\n"),S);
	       _exit(1);
	    }
	 }
	 else
	 {
	    char S[500];
	    snprintf(S,sizeof(S),"%s %s",
		     _config->Find("RPM::Source::Install-Command","rpm -ivh").c_str(),
		     Dsc[I].Dsc.c_str());
	    if (system(S) != 0)
	    {
	       fprintf(stderr,_("Unpack command '%s' failed.\n"),S);
	       _exit(1);
	    }
	 }
      }

      _exit(0);
   }

   // Wait for the subprocess
   int Status = 0;
   while (waitpid(Process,&Status,0) != Process)
   {
      if (errno == EINTR)
	 continue;
      return _error->Errno("waitpid","Couldn't wait for subprocess");
   }

   if (WIFEXITED(Status) == 0 || WEXITSTATUS(Status) != 0)
      return _error->Error(_("Child process failed"));

   return true;
}
									/*}}}*/
// DoBuildDep - Install/removes packages to satisfy build dependencies  /*{{{*/
// ---------------------------------------------------------------------
/* This function will look at the build depends list of the given source
   package and install the necessary packages to make it true, or fail. */
bool DoBuildDep(CommandLine &CmdL)
{
   CacheFile Cache(c1out,ShouldLockForInstall());
   // CNC:2004-04-06
   if (Cache.Open() == false ||
       Cache.CheckDeps() == false)
      return false;

   if (CmdL.FileSize() <= 1)
      return _error->Error(_("Must specify at least one package to check builddeps for"));

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

   unsigned J = 0;
   for (const char **I = CmdL.FileList + 1; *I != 0; I++, J++)
   {
      string Src;
      pkgSrcRecords::Parser *Last = FindSrc(*I,Recs,SrcRecs,Src,*Cache);
      if (Last == 0)
	 return _error->Error(_("Unable to find a source package for %s"),Src.c_str());

      // Process the build-dependencies
      vector<pkgSrcRecords::Parser::BuildDepRec> BuildDeps;
      if (Last->BuildDepends(BuildDeps, _config->FindB("APT::Get::Arch-Only",false)) == false)
	return _error->Error(_("Unable to get build-dependency information for %s"),Src.c_str());

      // Also ensure that build-essential packages are present
      Configuration::Item const *Opts = _config->Tree("APT::Build-Essential");
      if (Opts)
	 Opts = Opts->Child;
      for (; Opts; Opts = Opts->Next)
      {
	 if (Opts->Value.empty() == true)
	    continue;

         pkgSrcRecords::Parser::BuildDepRec rec;
	 rec.Package = Opts->Value;
	 rec.Type = pkgSrcRecords::Parser::BuildDependIndep;
	 rec.Op = 0;
	 BuildDeps.push_back(rec);
      }

      if (BuildDeps.size() == 0)
      {
	 ioprintf(c1out,_("%s has no build depends.\n"),Src.c_str());
	 continue;
      }

      // Install the requested packages
      unsigned int ExpectedInst = 0;
      vector <pkgSrcRecords::Parser::BuildDepRec>::iterator D;
      pkgProblemResolver Fix(Cache);
      bool skipAlternatives = false; // skip remaining alternatives in an or group
      for (D = BuildDeps.begin(); D != BuildDeps.end(); D++)
      {
         bool hasAlternatives = (((*D).Op & pkgCache::Dep::Or) == pkgCache::Dep::Or);

         if (skipAlternatives == true)
         {
            if (!hasAlternatives)
               skipAlternatives = false; // end of or group
            continue;
         }

         if ((*D).Type == pkgSrcRecords::Parser::BuildConflict ||
	     (*D).Type == pkgSrcRecords::Parser::BuildConflictIndep)
         {
            pkgCache::PkgIterator Pkg = Cache->FindPkg((*D).Package);
            // Build-conflicts on unknown packages are silently ignored
            if (Pkg.end() == true)
               continue;

            pkgCache::VerIterator IV = (*Cache)[Pkg].InstVerIter(*Cache);

            /*
             * Remove if we have an installed version that satisfies the
             * version criteria
             */
            if (IV.end() == false &&
                Cache->VS().CheckDep(IV.VerStr(),(*D).Op,(*D).Version.c_str()) == true)
               TryToInstall(Pkg,Cache,Fix,true,false,ExpectedInst);
         }
	 else // BuildDep || BuildDepIndep
         {
            if (_config->FindB("Debug::BuildDeps",false) == true)
                 cout << "Looking for " << (*D).Package << "...\n";

	    pkgCache::PkgIterator Pkg = Cache->FindPkg((*D).Package);

	    // CNC:2003-11-21 - Try to handle unknown file deps.
	    if (Pkg.end() == true && (*D).Package[0] == '/')
	    {
	       const char *File = (*D).Package.c_str();
	       Pkg = (*Cache).PkgBegin();
	       for (; Pkg.end() == false; Pkg++)
	       {
		  // Should we try on all versions?
		  pkgCache::VerIterator Ver = (*Cache)[Pkg].CandidateVerIter(*Cache);
		  if (Ver.end() == false)
		  {
		     pkgRecords::Parser &Parse = Recs.Lookup(Ver.FileList());
		     if (Parse.HasFile(File))
			break;
		  }
	       }
	    }

	    if (Pkg.end() == true)
            {
               if (_config->FindB("Debug::BuildDeps",false) == true)
                    cout << " (not found)" << (*D).Package << endl;

               if (hasAlternatives)
                  continue;

               return _error->Error(_("%s dependency for %s cannot be satisfied "
                                      "because the package %s cannot be found"),
                                    Last->BuildDepType((*D).Type),Src.c_str(),
                                    (*D).Package.c_str());
            }

            /*
             * if there are alternatives, we've already picked one, so skip
             * the rest
             *
             * TODO: this means that if there's a build-dep on A|B and B is
             * installed, we'll still try to install A; more importantly,
             * if A is currently broken, we cannot go back and try B. To fix
             * this would require we do a Resolve cycle for each package we
             * add to the install list. Ugh
             */

	    /*
	     * If this is a virtual package, we need to check the list of
	     * packages that provide it and see if any of those are
	     * installed
	     */
            pkgCache::PrvIterator Prv = Pkg.ProvidesList();
            for (; Prv.end() != true; Prv++)
	    {
               if (_config->FindB("Debug::BuildDeps",false) == true)
                    cout << "  Checking provider " << Prv.OwnerPkg().Name() << endl;

	       if ((*Cache)[Prv.OwnerPkg()].InstVerIter(*Cache).end() == false)
	          break;
            }

            // Get installed version and version we are going to install
	    pkgCache::VerIterator IV = (*Cache)[Pkg].InstVerIter(*Cache);

            if ((*D).Version[0] != '\0') {
                 // Versioned dependency

                 pkgCache::VerIterator CV = (*Cache)[Pkg].CandidateVerIter(*Cache);

                 for (; CV.end() != true; CV++)
                 {
                      if (Cache->VS().CheckDep(CV.VerStr(),(*D).Op,(*D).Version.c_str()) == true)
                           break;
                 }
                 if (CV.end() == true)
                 {
		   if (hasAlternatives)
		   {
		      continue;
		   }
		   else
		   {
                      return _error->Error(_("%s dependency for %s cannot be satisfied "
                                             "because no available versions of package %s "
                                             "can satisfy version requirements"),
                                           Last->BuildDepType((*D).Type),Src.c_str(),
                                           (*D).Package.c_str());
		   }
                 }
            }
            else
            {
               // Only consider virtual packages if there is no versioned dependency
               if (Prv.end() == false)
               {
                  if (_config->FindB("Debug::BuildDeps",false) == true)
                     cout << "  Is provided by installed package " << Prv.OwnerPkg().Name() << endl;
                  skipAlternatives = hasAlternatives;
                  continue;
               }
            }

            if (IV.end() == false)
            {
               if (_config->FindB("Debug::BuildDeps",false) == true)
                  cout << "  Is installed\n";

               if (Cache->VS().CheckDep(IV.VerStr(),(*D).Op,(*D).Version.c_str()) == true)
               {
                  skipAlternatives = hasAlternatives;
                  continue;
               }

               if (_config->FindB("Debug::BuildDeps",false) == true)
                  cout << "    ...but the installed version doesn't meet the version requirement\n";

               if (((*D).Op & pkgCache::Dep::LessEq) == pkgCache::Dep::LessEq)
               {
                  return _error->Error(_("Failed to satisfy %s dependency for %s: Installed package %s is too new"),
                                       Last->BuildDepType((*D).Type),
                                       Src.c_str(),
                                       Pkg.Name());
               }
            }


            if (_config->FindB("Debug::BuildDeps",false) == true)
               cout << "  Trying to install " << (*D).Package << endl;

            if (TryToInstall(Pkg,Cache,Fix,false,false,ExpectedInst) == true)
            {
               // We successfully installed something; skip remaining alternatives
               skipAlternatives = hasAlternatives;
               continue;
            }
            else if (hasAlternatives)
            {
               if (_config->FindB("Debug::BuildDeps",false) == true)
                  cout << "  Unsatisfiable, trying alternatives\n";
               continue;
            }
            else
            {
               return _error->Error(_("Failed to satisfy %s dependency for %s: %s"),
                                    Last->BuildDepType((*D).Type),
                                    Src.c_str(),
                                    (*D).Package.c_str());
            }
	 }
      }

      Fix.InstallProtect();
      if (Fix.Resolve(true) == false)
	 _error->Discard();

      // Now we check the state of the packages,
      if (Cache->BrokenCount() != 0)
      {
         // CNC:2004-07-05
         ShowBroken(cerr, Cache, false);
	 return _error->Error(_("Some broken packages were found while trying to process build-dependencies for %s.\n"
				"You might want to run `apt-get --fix-broken install' to correct these."),*I);
      }
   }

   if (InstallPackages(Cache, false, true) == false)
      return _error->Error(_("Failed to process build dependencies"));

   // CNC:2003-03-06
   if (CheckOnly(Cache) == true)
      return true;

   return true;
}
									/*}}}*/

// DoMoo - Never Ask, Never Tell					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool DoMoo(CommandLine &CmdL)
{
   cout <<
      "         (__) \n"
      "         (oo) \n"
      "   /------\\/ \n"
      "  / |    ||   \n"
      " *  /\\---/\\ \n"
      "    ~~   ~~   \n"
      "....\"Have you mooed today?\"...\n";

   return true;
}
									/*}}}*/

bool DoAutoremove(CommandLine &/*CmdL*/)
{
   CacheFile Cache(c1out,ShouldLockForInstall());

   if ((not Cache.Open()) || (not Cache.CheckDeps()))
   {
      return false;
   }

   c0out << _("Calculating Autoremove... ") << std::flush;
   if (not pkgAutoremove(*Cache))
   {
      c0out << _("Failed") << std::endl;
      ShowBroken(std::cerr, Cache, false);
      return false;
   }

   if (CheckOnly(Cache))
   {
      return true;
   }

   c0out << _("Done") << std::endl;

   return InstallPackages(Cache, false);
}

// CNC:2003-03-18
// DoScript - Scripting stuff.						/*{{{*/
// ---------------------------------------------------------------------
/* */
#ifdef WITH_LUA
bool DoScript(CommandLine &CmdL)
{
   for (const char **I = CmdL.FileList+1; *I != 0; I++)
      _config->Set("Scripts::AptGet::Script::", *I);

   _lua->SetGlobal("commit_ask", 1);
   _lua->RunScripts("Scripts::AptGet::Script");
   double Ask = _lua->GetGlobalNum("commit_ask");
   _lua->ResetGlobals();

   AptGetLuaCache *LuaCache = (AptGetLuaCache*) _lua->GetCacheControl();
   if (LuaCache && LuaCache->Cache) {
      CacheFile &Cache = *LuaCache->Cache;
      if (CheckOnly(Cache))
	 return true;
      if ((*Cache).InstCount() > 0 || (*Cache).DelCount() > 0)
	 return InstallPackages(Cache, false, Ask);
   }

   return true;
}
#endif
									/*}}}*/

// ShowHelp - Show a help screen					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool ShowHelp(CommandLine &CmdL)
{
   ioprintf(cout,_("%s %s for %s %s compiled on %s %s\n"),PACKAGE,VERSION,
	    COMMON_OS,COMMON_CPU,__DATE__,__TIME__);

   if (_config->FindB("version") == true)
   {
      cout << _("Supported Modules:") << endl;

      for (unsigned I = 0; I != pkgVersioningSystem::GlobalListLen; I++)
      {
	 pkgVersioningSystem *VS = pkgVersioningSystem::GlobalList[I];
	 if (_system != 0 && _system->VS == VS)
	    cout << '*';
	 else
	    cout << ' ';
	 cout << "Ver: " << VS->Label << endl;

	 /* Print out all the packaging systems that will work with
	    this VS */
	 for (unsigned J = 0; J != pkgSystem::GlobalListLen; J++)
	 {
	    pkgSystem *Sys = pkgSystem::GlobalList[J];
	    if (_system == Sys)
	       cout << '*';
	    else
	       cout << ' ';
	    if (Sys->VS->TestCompatibility(*VS) == true)
	       cout << "Pkg:  " << Sys->Label << " (Priority " << Sys->Score(*_config) << ")" << endl;
	 }
      }

      for (unsigned I = 0; I != pkgSourceList::Type::GlobalListLen; I++)
      {
	 pkgSourceList::Type *Type = pkgSourceList::Type::GlobalList[I];
	 cout << " S.L: '" << Type->Name << "' " << Type->Label << endl;
      }

      for (unsigned I = 0; I != pkgIndexFile::Type::GlobalListLen; I++)
      {
	 pkgIndexFile::Type *Type = pkgIndexFile::Type::GlobalList[I];
	 cout << " Idx: " << Type->Label << endl;
      }

      return true;
   }

   cout <<
    _("Usage: apt-get [options] command\n"
      "       apt-get [options] install|remove pkg1 [pkg2 ...]\n"
      "       apt-get [options] source pkg1 [pkg2 ...]\n"
      "\n"
      "apt-get is a simple command line interface for downloading and\n"
      "installing packages. The most frequently used commands are update\n"
      "and install.\n"
      "\n"
      "Commands:\n"
      "   update - Retrieve new lists of packages\n"
      "   upgrade - Perform an upgrade\n"
// CNC:2003-02-20 - Use .rpm extension in documentation.
      "   install - Install new packages (pkg is libc6 not libc6.rpm)\n"
      "   remove - Remove packages\n"
      "   source - Download source archives\n"
      "   build-dep - Configure build-dependencies for source packages\n"
      "   dist-upgrade - Distribution upgrade, see apt-get(8)\n"
// CNC:2002-08-01
//      "   dselect-upgrade - Follow dselect selections\n"
      "   clean - Erase downloaded archive files\n"
      "   autoclean - Erase old downloaded archive files\n"
      "   check - Verify that there are no broken dependencies\n"
// CNC:2003-03-16
      );
#ifdef WITH_LUA
      _lua->RunScripts("Scripts::AptGet::Help::Command");
#endif
      cout << _(
      "\n"
      "Options:\n"
      "  -h  This help text.\n"
      "  -q  Loggable output - no progress indicator\n"
      "  -qq No output except for errors\n"
      "  -d  Download only - do NOT install or unpack archives\n"
      "  -s  No-act. Perform ordering simulation\n"
      "  -y  Assume Yes to all queries and do not prompt\n"
      "  -f  Attempt to continue if the integrity check fails\n"
      "  -m  Attempt to continue if archives are unlocatable\n"
      "  -u  Show a list of upgraded packages as well\n"
      "  -b  Build the source package after fetching it\n"
// CNC:2002-08-02
      "  -D  When removing packages, remove dependencies as possible\n"
      "  -V  Show verbose version numbers\n"
      "  -c=? Read this configuration file\n"
      "  -o=? Set an arbitary configuration option, eg -o dir::cache=/tmp\n"
      "See the apt-get(8), sources.list(5) and apt.conf(5) manual\n"
      "pages for more information and options.\n"
      "                       This APT has Super Cow Powers.\n");
   return true;
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
   /* This option was apparently misspelled in the old source code and
      hence had no effect anyway; moreover, usually the opposite one is checked
      with the default value being true, so this setting here contradicted
      the intentions in the rest of the code.
      Cf. this file or the new apt-pkg/update.cc:

       (_config->FindB("APT::Get::List-Cleanup", true) &&
        _config->FindB("APT::List-Cleanup", true))
   */
   //_config->Set("APT::Get::No-List-Cleanup",true);
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
      {'V',"verbose-versions","APT::Get::Show-Versions",0},
      {'q',"quiet","quiet",CommandLine::IntLevel},
      { 0, "simple-output","simple-output",0},
      {'q',"silent","quiet",CommandLine::IntLevel},
      {'d',"download-only","APT::Get::Download-Only",0},
      {'b',"compile","APT::Get::Compile",0},
      {'b',"build","APT::Get::Compile",0},
      {'s',"simulate","APT::Get::Simulate",0},
      {'s',"just-print","APT::Get::Simulate",0},
      {'s',"recon","APT::Get::Simulate",0},
      {'s',"dry-run","APT::Get::Simulate",0},
      {'s',"no-act","APT::Get::Simulate",0},
      {'y',"yes","APT::Get::Assume-Yes",0},
      {'y',"assume-yes","APT::Get::Assume-Yes",0},
      {'f',"fix-broken","APT::Get::Fix-Broken",0},
      {'u',"show-upgraded","APT::Get::Show-Upgraded",0},
      {'m',"ignore-missing","APT::Get::Fix-Missing",0},
      {'D',"remove-deps","APT::Remove-Depends",0}, // CNC:2002-08-01
      {'t',"target-release","APT::Default-Release",CommandLine::HasArg},
      {'t',"default-release","APT::Default-Release",CommandLine::HasArg},
      {0,"download","APT::Get::Download",0},
      {0,"fix-missing","APT::Get::Fix-Missing",0},
      {0,"ignore-hold","APT::Ignore-Hold",0},
      {0,"upgrade","APT::Get::upgrade",0},
      {0,"force-yes","APT::Get::force-yes",0},
      {0,"print-uris","APT::Get::Print-URIs",0},
      {0,"purge","APT::Get::Purge",0},
      {0,"list-cleanup","APT::Get::List-Cleanup",0},
      {0,"reinstall","APT::Get::ReInstall",0},
      {0,"trivial-only","APT::Get::Trivial-Only",0},
      {0, "autoremove", "APT::Get::AutomaticRemove", 0},
      {0, "auto-remove", "APT::Get::AutomaticRemove", 0},
      {0,"remove","APT::Get::Remove",0},
      {0,"only-source","APT::Get::Only-Source",0},
      {0,"arch-only","APT::Get::Arch-Only",0},
      {0,"check-only","APT::Get::Check-Only",0}, // CNC:2003-03-06
      {'c',"config-file",0,CommandLine::ConfigFile},
      {'o',"option",0,CommandLine::ArbItem},
      {0,"manifest","manifest",CommandLine::HasArg},
      {0, "enable-upgrade", "APT::Get::EnableUpgrade", 0},
      {0,0,0,0}};
   CommandLine::Dispatch Cmds[] = {{"update",&DoUpdate},
                                   {"upgrade",&DoUpgrade},
                                   {"install",&DoInstall},
                                   // CNC:2004-03-22
                                   {"reinstall",&DoInstall},
                                   {"remove",&DoInstall},
                                   {"dist-upgrade",&DoDistUpgrade},
                                   {"dselect-upgrade",&DoDSelectUpgrade},
				   {"build-dep",&DoBuildDep},
                                   {"clean",&DoClean},
                                   {"autoclean",&DoAutoClean},
                                   {"check",&DoCheck},
				   {"source",&DoSource},
				   {"moo",&DoMoo},
				   {"autoremove", &DoAutoremove},
				   {"help",&ShowHelp},
// CNC:2003-03-19
#ifdef WITH_LUA
				   {"script",&DoScript},
#endif
                                   {0,0}};

   // Set up gettext support
   setlocale(LC_ALL,"");
   textdomain(PACKAGE);

   // Parse the command line and initialize the package library
   CommandLine CmdL(Args,_config);
   if (pkgInitConfig(*_config) == false ||
       pkgInitSystem(*_config,_system) == false ||
       CmdL.Parse(argc,argv) == false)
   {
      if (_config->FindB("version") == true)
	 ShowHelp(CmdL);

      _error->DumpErrors();
      return 100;
   }

   //append manifest content to FileList
   if (!_config->Find("manifest").empty())
   {
     std::ifstream is(_config->Find("manifest").c_str());
     vector<char*> new_filelist;
     std::string line;

     for (const char **I = CmdL.FileList; *I != 0; I++)
       new_filelist.push_back(strdup(*I));
     while (std::getline(is,line))
        new_filelist.push_back(strdup(line.c_str()));

     CmdL.FreeFileList();

     size_t len = new_filelist.size();
     CmdL.FileList = new const char *[len + 1];
     for(size_t i=0; i<len; ++i)
        CmdL.FileList[i] = new_filelist[i];
     CmdL.FileList[len] = 0;
   }

   // See if the help should be shown
   if (_config->FindB("help") == true ||
       _config->FindB("version") == true ||
       CmdL.FileSize() == 0)
   {
      ShowHelp(CmdL);
      return 0;
   }

   // CNC:2003-11-21
   if (CmdL.FileSize() > 1)
   {
      // CNC:2003-11-23
      vector<string> URLLst;
      for (const char **I = CmdL.FileList + 1; *I != 0; I++)
      {
	 if (strstr(*I, "://") != NULL)
	 {
	    URLLst.push_back(*I);
	    const char *N = strdup(strrchr(*I, '/')+1);
	    free((void *)*I);
	    *I = N;
	 }
      }

      if (URLLst.empty() == false && DownloadPackages(URLLst) == false)
      {
	    _error->DumpErrors();
	    return 100;
      }

      for (const char **I = CmdL.FileList + 1; *I != 0; I++)
	 _config->Set("APT::Arguments::", *I);
   }

   // Deal with stdout not being a tty
   if (ttyname(STDOUT_FILENO) == 0 && _config->FindI("quiet",0) < 1)
      _config->Set("quiet","1");

   // Setup the output streams
   if (_config->FindB("simple-output"))
   {
	c0out.rdbuf(devnull.rdbuf());
	c1out.rdbuf(devnull.rdbuf());
	c2out.rdbuf(devnull.rdbuf());
	c3out.rdbuf(cout.rdbuf());
   }
   else
   {
        c0out.rdbuf(cout.rdbuf());
        c1out.rdbuf(cout.rdbuf());
        c2out.rdbuf(cout.rdbuf());
        c3out.rdbuf(devnull.rdbuf());
   }

   if (_config->FindI("quiet",0) > 0)
      c0out.rdbuf(devnull.rdbuf());
   if (_config->FindI("quiet",0) > 1)
      c1out.rdbuf(devnull.rdbuf());

   // Setup the signals
   signal(SIGPIPE,SIG_IGN);
   signal(SIGWINCH,SigWinch);
   SigWinch(0);

// CNC:2003-11-23
#ifdef WITH_LUA
   AptGetLuaCache LuaCache;
   _lua->SetCacheControl(&LuaCache);

   double Consume = 0;
   if (argc > 1 && _lua->HasScripts("Scripts::AptGet::Command") == true)
   {
      _lua->SetGlobal("commit_ask", 1);
      _lua->SetGlobal("command_args", CmdL.FileList);
      _lua->SetGlobal("command_consume", 0.0);
      _lua->RunScripts("Scripts::AptGet::Command");
      Consume = _lua->GetGlobalNum("command_consume");
      double Ask = _lua->GetGlobalNum("commit_ask");
      _lua->ResetGlobals();
      _lua->ResetCaches();

      if (Consume == 1 && LuaCache.Cache)
      {
	 CacheFile &Cache = *LuaCache.Cache;
	 if (CheckOnly(Cache) == false &&
	     ((*Cache).InstCount() > 0 || (*Cache).DelCount() > 0))
	    InstallPackages(Cache, false, Ask);
      }
   }

   if (Consume == 0)
#endif
   // Match the operation
   CmdL.DispatchArg(Cmds);

   // Print any errors or warnings found during parsing
   if (_error->empty() == false)
   {
      bool Errors = _error->PendingError();
      _error->DumpErrors();
      return Errors == true?100:0;
   }

   return 0;
}

// vim:sts=3:sw=3
