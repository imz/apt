// Description								/*{{{*/
/* ######################################################################

   RPM Package Manager - Provide an interface to rpm

   #####################################################################
 */
									/*}}}*/
// Includes								/*{{{*/
#ifdef __GNUG__
#pragma implementation "apt-pkg/rpmpm.h"
#endif

#include <config.h>

#ifdef HAVE_RPM

#include <apt-pkg/rpmpm.h>
#include <apt-pkg/error.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/luaiface.h>
#include <apt-pkg/depcache.h>
#include <apt-pkg/scopeexit.h>

#include <apti18n.h>

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <iostream>
#include <cstring>
#include <functional>
#include <utility>

#include <rpm/rpmdb.h>
#include <rpm/rpmlog.h>

namespace {

uint32_t collect_autoinstalled_flag(pkgDepCache &Cache, pkgCache::PkgIterator Pkg)
{
   return (Cache.getMarkAuto(Pkg) == pkgDepCache::AutoMarkFlag::Auto) ? 1 : 0;
}

std::string rpm_name_conversion(const pkgCache::PkgIterator &Pkg)
{
   string Name = Pkg.Name();
   string::size_type loc;
   bool NeedLabel = false;

   // Unmunge our package names so rpm can find them...
   if ((loc = Name.find('#')) != string::npos) {
      Name = Name.substr(0, loc);
      NeedLabel = true;
   }
   if ((loc = Name.rfind(".32bit")) != string::npos &&
      loc == Name.length() - strlen(".32bit"))
      Name = Name.substr(0,loc);
   if (NeedLabel) {
      const char *VerStr = Pkg.CurrentVer().VerStr();
      Name += "-";
      Name += VerStr;
   }

   // This is needed for removal to work on multilib packages, but old
   // rpm versions don't support name.arch in RPMDBI_LABEL, oh well...
   Name = Name + "." + Pkg.CurrentVer().Arch();

   return Name;
}

} // unnamed namespace

// RPMPM::pkgRPMPM - Constructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgRPMPM::pkgRPMPM(pkgDepCache *Cache) : pkgPackageManager(Cache)
{
}
									/*}}}*/
// RPMPM::pkgRPMPM - Destructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgRPMPM::~pkgRPMPM()
{
}
									/*}}}*/
// RPMPM::Install - Install a package					/*{{{*/
// ---------------------------------------------------------------------
/* Add an install operation to the sequence list */
bool pkgRPMPM::Install(PkgIterator Pkg,string File)
{
   if (File.empty() == true || Pkg.end() == true)
      return _error->Error(_("Internal Error, No file name for %s"),Pkg.Name());

   List.push_back(Item(Item::Install,Pkg,File));
   return true;
}
									/*}}}*/
// RPMPM::Configure - Configure a package				/*{{{*/
// ---------------------------------------------------------------------
/* Add a configure operation to the sequence list */
bool pkgRPMPM::Configure(PkgIterator Pkg)
{
   if (Pkg.end() == true) {
      return false;
   }

   List.push_back(Item(Item::Configure,Pkg));
   return true;
}
									/*}}}*/
// RPMPM::Remove - Remove a package					/*{{{*/
// ---------------------------------------------------------------------
/* Add a remove operation to the sequence list */
bool pkgRPMPM::Remove(PkgIterator Pkg,bool Purge)
{
   if (Pkg.end() == true)
      return false;

   if (Purge == true)
      List.push_back(Item(Item::Purge,Pkg));
   else
      List.push_back(Item(Item::Remove,Pkg));
   return true;
}
									/*}}}*/
// RPMPM::RunScripts - Run a set of scripts				/*{{{*/
// ---------------------------------------------------------------------
/* This looks for a list of script sto run from the configuration file,
   each one is run with system from a forked child. */
bool pkgRPMPM::RunScripts(const char *Cnf)
{
   Configuration::Item const *Opts = _config->Tree(Cnf);
   if (Opts == 0 || Opts->Child == 0)
      return true;
   Opts = Opts->Child;

   bool error = false;
   for (; Opts != 0; Opts = Opts->Next)
   {
      if (Opts->Value.empty() == true)
         continue;

      // Purified Fork for running the script
      pid_t Process = ExecFork();
      if (Process == 0)
      {
	 if (chdir("/tmp") != 0)
	    _exit(100);

	 const char *Args[4];
	 Args[0] = "/bin/sh";
	 Args[1] = "-c";
	 Args[2] = Opts->Value.c_str();
	 Args[3] = 0;
	 execv(Args[0],(char **)Args);
	 _exit(100);
      }

      // Clean up the sub process
      if (ExecWait(Process,Opts->Value.c_str()) == false) {
	 _error->Error(_("Problem executing scripts %s '%s'"),Cnf,
		       Opts->Value.c_str());
	 error = true;
      }
   }

   // Restore sig int/quit
   signal(SIGQUIT,SIG_DFL);
   signal(SIGINT,SIG_DFL);

   if (error)
      return _error->Error(_("Sub-process returned an error code"));

   return true;
}

                                                                        /*}}}*/
// RPMPM::RunScriptsWithPkgs - Run scripts with package names on stdin /*{{{*/
// ---------------------------------------------------------------------
/* This looks for a list of scripts to run from the configuration file
   each one is run and is fed on standard input a list of all .deb files
   that are due to be installed. */
bool pkgRPMPM::RunScriptsWithPkgs(const char *Cnf)
{
   Configuration::Item const *Opts = _config->Tree(Cnf);
   if (Opts == 0 || Opts->Child == 0)
      return true;
   Opts = Opts->Child;

   for (; Opts != 0; Opts = Opts->Next)
   {
      if (Opts->Value.empty() == true)
         continue;

      // Create the pipes
      int Pipes[2];
      if (pipe(Pipes) != 0)
	 return _error->Errno("pipe",_("Failed to create IPC pipe to subprocess"));
      SetCloseExec(Pipes[0],true);
      SetCloseExec(Pipes[1],true);

      // Purified Fork for running the script
      pid_t Process = ExecFork();
      if (Process == 0)
      {
	 // Setup the FDs
	 dup2(Pipes[0],STDIN_FILENO);
	 SetCloseExec(STDOUT_FILENO,false);
	 SetCloseExec(STDIN_FILENO,false);
	 SetCloseExec(STDERR_FILENO,false);

	 const char *Args[4];
	 Args[0] = "/bin/sh";
	 Args[1] = "-c";
	 Args[2] = Opts->Value.c_str();
	 Args[3] = 0;
	 execv(Args[0],(char **)Args);
	 _exit(100);
      }
      close(Pipes[0]);
      FileFd Fd(Pipes[1]);

      // Feed it the filenames.
      for (vector<Item>::iterator I = List.begin(); I != List.end(); I++)
      {
	 // Only deal with packages to be installed from .rpm
	 if (I->Op != Item::Install)
	    continue;

	 // No errors here..
	 if (I->File[0] != '/')
	    continue;

	 /* Feed the filename of each package that is pending install
	    into the pipe. */
	 if (Fd.Write(I->File.c_str(),I->File.length()) == false ||
	     Fd.Write("\n",1) == false)
	 {
	    kill(Process,SIGINT);
	    Fd.Close();
	    ExecWait(Process,Opts->Value.c_str(),true);
	    return _error->Error(_("Failure running script %s"),Opts->Value.c_str());
	 }
      }
      Fd.Close();

      // Clean up the sub process
      if (ExecWait(Process,Opts->Value.c_str()) == false)
	 return _error->Error(_("Failure running script %s"),Opts->Value.c_str());
   }

   return true;
}

									/*}}}*/


// RPMPM::Go - Run the sequence						/*{{{*/
// ---------------------------------------------------------------------
/* This globs the operations and calls rpm */
bool pkgRPMPM::Go()
{
   if (List.empty() == true)
      return true;

   if (RunScripts("RPM::Pre-Invoke") == false)
      return false;

   if (RunScriptsWithPkgs("RPM::Pre-Install-Pkgs") == false)
      return false;

   vector<apt_item> install_or_upgrade;
   vector<apt_item> install;
   vector<apt_item> upgrade;
   vector<apt_item> uninstall;
   vector<pkgCache::Package*> pkgs_install;
   vector<pkgCache::Package*> pkgs_uninstall;

   vector<char*> unalloc;

   for (vector<Item>::iterator I = List.begin(); I != List.end(); I++)
   {
      string Name = I->Pkg.Name();
      string::size_type loc;

      switch (I->Op)
      {
      case Item::Purge:
      case Item::Remove:
	 Name = rpm_name_conversion(I->Pkg);
	 uninstall.push_back(apt_item(Name.c_str(), collect_autoinstalled_flag(Cache, I->Pkg)));
	 unalloc.push_back(strdup(Name.c_str()));
	 pkgs_uninstall.push_back(I->Pkg);
	 break;

       case Item::Configure:
	 break;

       case Item::Install:
	 if ((loc = Name.find('#')) != string::npos) {
	    Name = Name.substr(0,loc);
	    PkgIterator Pkg = Cache.FindPkg(Name);
	    PrvIterator Prv = Pkg.ProvidesList();
	    bool Installed = false;
	    for (; Prv.end() == false; Prv++) {
	       if (Prv.OwnerPkg().CurrentVer().end() == false) {
		  Installed = true;
		  break;
	       }
	    }
	    if (Installed)
	       install.push_back(apt_item(I->File.c_str(), collect_autoinstalled_flag(Cache, I->Pkg)));
	    else
	       upgrade.push_back(apt_item(I->File.c_str(), collect_autoinstalled_flag(Cache, I->Pkg)));
	 } else {
	    upgrade.push_back(apt_item(I->File.c_str(), collect_autoinstalled_flag(Cache, I->Pkg)));
	 }
	 install_or_upgrade.push_back(apt_item(I->File.c_str(), collect_autoinstalled_flag(Cache, I->Pkg)));
	 pkgs_install.push_back(I->Pkg);
	 break;

       default:
	 return _error->Error(_("Unknown pkgRPMPM operation."));
      }
   }

   bool Ret = true;

#ifdef WITH_LUA
   if (_lua->HasScripts("Scripts::PM::Pre") == true) {
      _lua->SetGlobal("files_install", install_or_upgrade);
      _lua->SetGlobal("names_remove", uninstall);
      _lua->SetGlobal("pkgs_install", pkgs_install);
      _lua->SetGlobal("pkgs_remove", pkgs_uninstall);
      _lua->SetDepCache(&Cache);
      _lua->RunScripts("Scripts::PM::Pre");
      _lua->ResetCaches();
      _lua->ResetGlobals();
      if (_error->PendingError() == true) {
	 _error->DumpErrors();
	 Ret = false;
	 goto exit;
      }
   }
#endif

   if (Process(install, upgrade, uninstall) == false)
      Ret = false;

#ifdef WITH_LUA
   if (_lua->HasScripts("Scripts::PM::Post") == true) {
      _lua->SetGlobal("files_install", install_or_upgrade);
      _lua->SetGlobal("names_remove", uninstall);
      _lua->SetGlobal("pkgs_install", pkgs_install);
      _lua->SetGlobal("pkgs_remove", pkgs_uninstall);
      _lua->SetGlobal("transaction_success", Ret);
      _lua->SetDepCache(&Cache);
      _lua->RunScripts("Scripts::PM::Post");
      _lua->ResetCaches();
      _lua->ResetGlobals();
      if (_error->PendingError() == true) {
	 _error->DumpErrors();
	 Ret = false;
	 goto exit;
      }
   }
#endif


   if (Ret == true)
      Ret = RunScripts("RPM::Post-Invoke");

exit:
   for (vector<char *>::iterator I = unalloc.begin(); I != unalloc.end(); I++)
      free(*I);

   return Ret;
}
									/*}}}*/
// pkgRPMPM::Reset - Dump the contents of the command list		/*{{{*/
// ---------------------------------------------------------------------
/* */
void pkgRPMPM::Reset()
{
   List.erase(List.begin(),List.end());
}
									/*}}}*/
// RPMExtPM::pkgRPMExtPM - Constructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgRPMExtPM::pkgRPMExtPM(pkgDepCache *Cache) : pkgRPMPM(Cache)
{
}
									/*}}}*/
// RPMExtPM::pkgRPMExtPM - Destructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgRPMExtPM::~pkgRPMExtPM()
{
}
									/*}}}*/

bool pkgRPMExtPM::ExecRPM(Item::RPMOps op, const std::vector<apt_item> &files)
{
   const char *Args[10000];
   const char *operation;
   unsigned int n = 0;
   bool Interactive = _config->FindB("RPM::Interactive",true);
   bool FancyPercent = _config->FindB("RPM::FancyPercent", false);
   int quiet = _config->FindI("quiet",0);

   string rpmbinary = _config->Find("Dir::Bin::rpm","rpm");
   Args[n++] = rpmbinary.c_str();

   bool nodeps = false;

   switch (op)
   {
      case Item::RPMInstall:
	 operation = "-i";
	 nodeps = true;
	 break;

      case Item::RPMUpgrade:
	 operation = "-U";
	 break;

      case Item::RPMErase:
	 operation = "-e";
	 nodeps = true;
	 break;
	default:
	 return false;
   }
   Args[n++] = operation;

	if (quiet <= 2 && op != Item::RPMErase)
	{
		Args[n++] = "-v";
		if (quiet <= 1)
		{
			if (Interactive)
			{
				Args[n++] = "-h";
				if (quiet <= 0 && FancyPercent)
					Args[n++] = "--fancypercent";
			} else
			{
				Args[n++] = "--percent";
			}
		}
	}

   string rootdir = _config->Find("RPM::RootDir", "");
   if (!rootdir.empty())
   {
       Args[n++] = "-r";
       Args[n++] = rootdir.c_str();
   }

   Configuration::Item const *Opts;
   if (op == Item::RPMErase)
   {
      Opts = _config->Tree("RPM::Erase-Options");
      if (Opts != 0)
      {
	 Opts = Opts->Child;
	 for (; Opts != 0; Opts = Opts->Next)
	 {
	    if (Opts->Value == "--nodeps")
	       nodeps = false;
	    else if (Opts->Value.empty() == true)
	       continue;
	    Args[n++] = Opts->Value.c_str();
	 }
      }
   }
   else
   {
      bool oldpackage = _config->FindB("RPM::OldPackage",
				       (op == Item::RPMUpgrade));
      bool replacepkgs = _config->FindB("APT::Get::ReInstall",false);
      bool replacefiles = _config->FindB("APT::Get::ReInstall",false);
      Opts = _config->Tree("RPM::Install-Options");
      if (Opts != 0)
      {
	 Opts = Opts->Child;
	 for (; Opts != 0; Opts = Opts->Next)
	 {
	    if (Opts->Value == "--oldpackage")
	       oldpackage = false;
	    else if (Opts->Value == "--replacepkgs")
	       replacepkgs = false;
	    else if (Opts->Value == "--replacefiles")
	       replacefiles = false;
	    else if (Opts->Value == "--nodeps")
	       nodeps = false;
	    else if (Opts->Value.empty() == true)
	       continue;
	    Args[n++] = Opts->Value.c_str();
	 }
      }
      if (oldpackage == true)
	 Args[n++] = "--oldpackage";
      if (replacepkgs == true)
	 Args[n++] = "--replacepkgs";
      if (replacefiles == true)
	 Args[n++] = "--replacefiles";
   }

   if (nodeps == true)
      Args[n++] = "--nodeps";

   Opts = _config->Tree("RPM::Options");
   if (Opts != 0)
   {
      Opts = Opts->Child;
      for (; Opts != 0; Opts = Opts->Next)
      {
	 if (Opts->Value.empty() == true)
	    continue;
	 Args[n++] = Opts->Value.c_str();
      }
   }

   if (_config->FindB("RPM::Order", true) == false)
      Args[n++] = "--noorder";

    string cmd;
    for (unsigned i = 0; i < n; ++i)
    {
	if (!cmd.empty())
	    cmd += ' ';
	cmd += Args[i];
    }

   bool FilesInArgs = true;
   char *ArgsFileName = NULL;
   if (op != Item::RPMErase && files.size() > 50) {
      string FileName = _config->FindDir("Dir::Cache", "/tmp/") +
			"filelist.XXXXXX";
      ArgsFileName = strdup(FileName.c_str());
      if (ArgsFileName) {
	 int fd = mkstemp(ArgsFileName);
	 if (fd != -1) {
	    FileFd File(fd);
	    for (auto I = files.begin();
		 I != files.end(); I++) {
	       File.Write(I->file.c_str(), I->file.length());
	       File.Write("\n", 1);
	    }
	    File.Close();
	    FilesInArgs = false;
	    Args[n++] = ArgsFileName;
	 }
      }
   }

   if (FilesInArgs == true) {
      for (auto I = files.begin();
	   I != files.end(); I++)
	 Args[n++] = I->file.c_str();
   }

   Args[n++] = 0;

   if (_config->FindB("Debug::pkgRPMPM",false) == true)
   {
      for (unsigned int k = 0; k < n; k++)
	  clog << Args[k] << ' ';
      clog << endl;
      if (ArgsFileName) {
	 unlink(ArgsFileName);
	 free(ArgsFileName);
      }
      return true;
   }

   if (quiet <= 2)
   cout << _("Executing RPM (")<<cmd<<")..." << endl;

   cout << flush;
   clog << flush;
   cerr << flush;

   /* Mask off sig int/quit. We do this because dpkg also does when
    it forks scripts. What happens is that when you hit ctrl-c it sends
    it to all processes in the group. Since dpkg ignores the signal
    it doesn't die but we do! So we must also ignore it */
   //akk ??
   signal(SIGQUIT,SIG_IGN);
   signal(SIGINT,SIG_IGN);

   // Fork rpm
   pid_t Child = ExecFork();

   // This is the child
   if (Child == 0)
   {
      if (chdir(_config->FindDir("RPM::Run-Directory","/").c_str()) != 0)
	  _exit(100);

      if (_config->FindB("RPM::FlushSTDIN",true) == true)
      {
	 int Flags,dummy;
	 if ((Flags = fcntl(STDIN_FILENO,F_GETFL,dummy)) < 0)
	     _exit(100);

	 // Discard everything in stdin before forking dpkg
	 if (fcntl(STDIN_FILENO,F_SETFL,Flags | O_NONBLOCK) < 0)
	     _exit(100);

	 while (read(STDIN_FILENO,&dummy,1) == 1);

	 if (fcntl(STDIN_FILENO,F_SETFL,Flags & (~(long)O_NONBLOCK)) < 0)
	     _exit(100);
      }

      execvp(Args[0],(char **)Args);
      cerr << _("Could not exec ") << Args[0] << endl;
      _exit(100);
   }

   // Wait for rpm
   int Status = 0;
   while (waitpid(Child,&Status,0) != Child)
   {
      if (errno == EINTR)
	  continue;
      RunScripts("RPM::Post-Invoke");
      if (ArgsFileName) {
	 unlink(ArgsFileName);
	 free(ArgsFileName);
      }
      return _error->Errno("waitpid",_("Couldn't wait for subprocess"));
   }
   if (ArgsFileName) {
      unlink(ArgsFileName);
      free(ArgsFileName);
   }

   // Restore sig int/quit
   signal(SIGQUIT,SIG_DFL);
   signal(SIGINT,SIG_DFL);

   // Check for an error code.
   if (WIFEXITED(Status) == 0 || WEXITSTATUS(Status) != 0)
   {
      RunScripts("RPM::Post-Invoke");
      if (WIFSIGNALED(Status) != 0 && WTERMSIG(Status) == SIGSEGV)
	  return _error->Error(_("Sub-process %s recieved a segmentation fault."),Args[0]);

      if (WIFEXITED(Status) != 0)
	  return _error->Error(_("Sub-process %s returned an error code (%u)"),Args[0],
			       WEXITSTATUS(Status));

      return _error->Error(_("Sub-process %s exited unexpectedly"),Args[0]);
   }

   if (quiet <= 2)
      cout << _("Done.") << endl;

   return true;
}

bool pkgRPMExtPM::Process(const std::vector<apt_item> &install,
		       const std::vector<apt_item> &upgrade,
		       const std::vector<apt_item> &uninstall)
{
   if (uninstall.empty() == false)
       ExecRPM(Item::RPMErase, uninstall);
   if (install.empty() == false)
       ExecRPM(Item::RPMInstall, install);
   if (upgrade.empty() == false)
       ExecRPM(Item::RPMUpgrade, upgrade);
   return true;
}

// RPMLibPM::pkgRPMLibPM - Constructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgRPMLibPM::pkgRPMLibPM(pkgDepCache *Cache) : pkgRPMPM(Cache)
{
}
									/*}}}*/
// RPMLibPM::pkgRPMLibPM - Destructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgRPMLibPM::~pkgRPMLibPM()
{
}
									/*}}}*/

bool pkgRPMLibPM::AddToTransaction(Item::RPMOps op, const std::vector<apt_item> &files)
{
   int rc;
   FD_t fd;
   rpmHeader hdr;

   for (auto I = files.begin(); I != files.end(); I++)
   {
      int upgrade = 0;

      switch (op)
      {
	 case Item::RPMUpgrade:
	    upgrade = 1;
	 case Item::RPMInstall:
	  {
	    fd = Fopen(I->file.c_str(), "r.ufdio");
	    if (fd == NULL)
	    {
	       _error->Error(_("Failed opening %s"), I->file.c_str());
	       return false;
	    }

		scope_exit close_fd(std::bind(&Fclose, fd));

            rc = rpmReadPackageFile(TS, fd, I->file.c_str(), &hdr);
	    if (rc != RPMRC_OK && rc != RPMRC_NOTTRUSTED && rc != RPMRC_NOKEY)
		{
	       _error->Error(_("Failed reading file %s"), I->file.c_str());
		   return false;
		}

		scope_exit free_hd(std::bind(&headerFree, hdr));

		rc = headerPutUint32(hdr, RPMTAG_AUTOINSTALLED, &(I->autoinstalled), 1);
		if (rc != 1)
		{
			_error->Error(_("Failed to add package flags for file %s"), I->file.c_str());
			return false;
		}

	    rc = rpmtsAddInstallElement(TS, hdr, I->file.c_str(), upgrade, 0);
	    if (rc)
		{
	       _error->Error(_("Failed adding %s to transaction %s"),
			     I->file.c_str(), "(install)");
	       return false;
		}
	  }
	  break;

	 case Item::RPMErase:
            rpmdbMatchIterator MI;
	    MI = rpmtsInitIterator(TS, (rpmTag)RPMDBI_LABEL, I->file.c_str(), 0);
	    while ((hdr = rpmdbNextIterator(MI)) != NULL)
	    {
	       unsigned int recOffset = rpmdbGetIteratorOffset(MI);
	       if (recOffset) {
		  rc = rpmtsAddEraseElement(TS, hdr, recOffset);
		  if (rc)
		  {
		     _error->Error(_("Failed adding %s to transaction %s"),
				   I->file.c_str(), "(erase)");
			 return false;
		  }
	       }
	    }
	    MI = rpmdbFreeIterator(MI);
	  break;
      }
   }
   return true;
}

bool pkgRPMLibPM::Process(const std::vector<apt_item> &install,
			  const std::vector<apt_item> &upgrade,
			  const std::vector<apt_item> &uninstall)
{
   int rc = 0;
   bool Success = false;
   bool Interactive = _config->FindB("RPM::Interactive",true);
   string Dir = _config->Find("RPM::RootDir");
   int quiet = _config->FindI("quiet",0);
   rpmReadConfigFiles(NULL, NULL);

   int probFilter = 0;
   unsigned int notifyFlags = 0;
   int tsFlags = 0;

   if (uninstall.empty() == false)
      ParseRpmOpts("RPM::Erase-Options", &tsFlags, &probFilter);
   if (install.empty() == false || upgrade.empty() == false)
      ParseRpmOpts("RPM::Install-Options", &tsFlags, &probFilter);
   ParseRpmOpts("RPM::Options", &tsFlags, &probFilter);

   rpmps probs;
   TS = rpmtsCreate();
   rpmtsSetVSFlags(TS, (rpmVSFlags_e)-1);
   // 4.1 needs this always set even if NULL,
   // otherwise all scriptlets fail
   rpmtsSetRootDir(TS, Dir.c_str());
   if (rpmtsOpenDB(TS, O_RDWR) != 0)
   {
      _error->Error(_("Could not open RPM database"));
      goto exit;
   }

   if (_config->FindB("RPM::OldPackage", true) || !upgrade.empty()) {
      probFilter |= RPMPROB_FILTER_OLDPACKAGE;
   }
   if (_config->FindB("APT::Get::ReInstall", false)) {
      probFilter |= RPMPROB_FILTER_REPLACEPKG;
      probFilter |= RPMPROB_FILTER_REPLACEOLDFILES;
      probFilter |= RPMPROB_FILTER_REPLACENEWFILES;
   }

    if (quiet <= 2)
	notifyFlags |= INSTALL_LABEL;

    if (quiet <= 1)
    {
	if (Interactive == true)
	{
	    notifyFlags |= INSTALL_HASH;
//	    extern int fancyPercents;
//	    fancyPercents = (quiet <= 0) ? 1 : 0;
	} else
	{
		notifyFlags |= INSTALL_PERCENT;
	}
    }

   if (uninstall.empty() == false)
   {
       if (not AddToTransaction(Item::RPMErase, uninstall))
       {
          goto exit;
       }
   }
   if (install.empty() == false)
   {
       if (not AddToTransaction(Item::RPMInstall, install))
       {
          goto exit;
       }
   }
   if (upgrade.empty() == false)
   {
       if (not AddToTransaction(Item::RPMUpgrade, upgrade))
       {
          goto exit;
       }
   }

   // Setup the gauge used by rpmShowProgress.
   //packagesTotal = install.size()+upgrade.size();

   if (_config->FindB("RPM::NoDeps", false) == false) {
      rc = rpmtsCheck(TS);
      probs = rpmtsProblems(TS);
      if (rc || rpmpsNumProblems(probs) > 0) {
	 rpmpsPrint(NULL, probs);
	 rpmpsFree(probs);
	 _error->Error(_("Transaction set check failed"));
	 goto exit;
      }
   }

   rc = 0;
   if (_config->FindB("RPM::Order", true) == true)
      rc = rpmtsOrder(TS);

   if (rc > 0) {
      _error->Error(_("Ordering failed for %d packages"), rc);
      goto exit;
   }

   if (quiet <= 2)
   cout << _("Committing changes...") << endl << flush;

   probFilter |= rpmtsFilterFlags(TS);
   rpmtsSetFlags(TS, (rpmtransFlags)(rpmtsFlags(TS) | tsFlags));
   rpmtsClean(TS);
   rc = rpmtsSetNotifyCallback(TS, rpmShowProgress,
                               (void *) (unsigned long) notifyFlags);
   rc = rpmtsRun(TS, NULL, (rpmprobFilterFlags)probFilter);
   probs = rpmtsProblems(TS);

   if (rc > 0) {
      _error->Error(_("Error while running transaction"));
      if (rpmpsNumProblems(probs) > 0)
	 rpmpsPrint(stderr, probs);
   } else if (rc < 0) {
      _error->Error(_("Some errors occurred while running transaction"));
   } else {
      Success = true;
      if (quiet <= 2)
	 cout << _("Done.") << endl;
   }
   rpmpsFree(probs);

exit:

   rpmtsFree(TS);

   return Success;
}

bool pkgRPMLibPM::ParseRpmOpts(const char *Cnf, int *tsFlags, int *probFilter)
{
   Configuration::Item const *Opts = _config->Tree(Cnf);

   if (Opts != 0)
   {
      Opts = Opts->Child;
      for (; Opts != 0; Opts = Opts->Next)
      {
	 if (Opts->Value.empty() == true)
	    continue;
	 // Transaction set flags
	 if (Opts->Value == "--noscripts")
	    *tsFlags |= RPMTRANS_FLAG_NOSCRIPTS;
	 else if (Opts->Value == "--notriggers")
	    *tsFlags |= RPMTRANS_FLAG_NOTRIGGERS;
	 else if (Opts->Value == "--nodocs" ||
	          Opts->Value == "--excludedocs")
	    *tsFlags |= RPMTRANS_FLAG_NODOCS;
	 else if (Opts->Value == "--allfiles")
	    *tsFlags |= RPMTRANS_FLAG_ALLFILES;
	 else if (Opts->Value == "--justdb")
	    *tsFlags |= RPMTRANS_FLAG_JUSTDB;
	 else if (Opts->Value == "--test")
	    *tsFlags |= RPMTRANS_FLAG_TEST;
	 else if (Opts->Value == "--nomd5")
	    *tsFlags |= RPMTRANS_FLAG_NOMD5;
	 else if (Opts->Value == "--noconfigs" ||
	          Opts->Value == "--excludeconfigs")
	    *tsFlags |= RPMTRANS_FLAG_NOCONFIGS;

	 // Problem filter flags
	 else if (Opts->Value == "--replacefiles")
	 {
	    *probFilter |= RPMPROB_FILTER_REPLACEOLDFILES;
	    *probFilter |= RPMPROB_FILTER_REPLACENEWFILES;
	 }
	 else if (Opts->Value == "--replacepkgs")
	    *probFilter |= RPMPROB_FILTER_REPLACEPKG;
	 else if (Opts->Value == "--ignoresize")
	 {
	    *probFilter |= RPMPROB_FILTER_DISKSPACE;
	    *probFilter |= RPMPROB_FILTER_DISKNODES;
	 }
	 else if (Opts->Value == "--badreloc")
	    *probFilter |= RPMPROB_FILTER_FORCERELOCATE;

	 // Misc things having apt config counterparts
	 else if (Opts->Value == "--force")
	    _config->Set("APT::Get::ReInstall", true);
	 else if (Opts->Value == "--oldpackage")
	    _config->Set("RPM::OldPackage", true);
	 else if (Opts->Value == "--nodeps")
	    _config->Set("RPM::NoDeps", true);
	 else if (Opts->Value == "--noorder")
	    _config->Set("RPM::Order", false);
	 else if (Opts->Value == "-v") {
	    rpmIncreaseVerbosity();
	 } else if (Opts->Value == "-vv") {
	    rpmIncreaseVerbosity();
	    rpmIncreaseVerbosity();
	 } else if (Opts->Value == "-vvv") {
	    rpmIncreaseVerbosity();
	    rpmIncreaseVerbosity();
	    rpmIncreaseVerbosity();
	 }
	 // TODO: --root, --relocate, --prefix, --excludepath etc...

      }
   }
   return true;
}

bool pkgRPMLibPM::UpdateMarks()
{
   std::vector<apt_item> changed_packages;
   int rc;

   uint32_t important_flags = pkgCache::Flag::Auto;

   for (PkgIterator I = Cache.PkgBegin(); not I.end(); ++I)
   {
      // If package is installed and any important flag changed
      if ((I->CurrentState == pkgCache::State::Installed)
         && ((Cache[I].Flags & important_flags) != (I->Flags & important_flags)))
      {
         changed_packages.push_back(apt_item(rpm_name_conversion(I), collect_autoinstalled_flag(Cache, I)));
      }
   }

   if (changed_packages.empty())
   {
      return true;
   }

   std::string Dir = _config->Find("RPM::RootDir");

   if (rpmReadConfigFiles(NULL, NULL) != 0)
   {
      return false;
   }

   TS = rpmtsCreate();

   scope_exit free_ts(std::bind(&rpmtsFree, TS));

   rpmtsSetVSFlags(TS, (rpmVSFlags_e)-1);
   // 4.1 needs this always set even if NULL,
   // otherwise all scriptlets fail
   rpmtsSetRootDir(TS, Dir.c_str());
   if (rpmtsOpenDB(TS, O_RDWR) != 0)
   {
      _error->Error(_("Could not open RPM database"));
      return false;
   }

   for (auto iter = changed_packages.begin(); iter != changed_packages.end(); ++iter)
   {
      size_t changed_count = 0;

      rpmdbMatchIterator MI;

      MI = rpmtsInitIterator(TS, (rpmTag)RPMDBI_LABEL, iter->file.c_str(), 0);

      scope_exit free_mi(std::bind(&rpmdbFreeIterator, MI));

      rpmdbSetIteratorRewrite(MI, 1);

      rpmHeader hdr;

      while ((hdr = rpmdbNextIterator(MI)) != NULL)
      {
         ++changed_count;

         rpmtd td = rpmtdNew();

         scope_exit free_td(std::bind(rpmtdFree, td));

         rc = headerGet(hdr, RPMTAG_AUTOINSTALLED, td, HEADERGET_MINMEM);
         if (rc == 1)
         {
            // An element found, modify it
            scope_exit free_td_data(std::bind(rpmtdFreeData, td));

            uint32_t *value = rpmtdGetUint32(td);
            if (value == nullptr)
            {
               _error->Error(_("Failed to change package flags for file %s"), iter->file.c_str());
               return false;
            }

            *value = iter->autoinstalled;
         }
         else
         {
            // No element found, add one
            rc = headerPutUint32(hdr, RPMTAG_AUTOINSTALLED, &(iter->autoinstalled), 1);
            if (rc != 1)
            {
               _error->Error(_("Failed to add package flags for file %s"), iter->file.c_str());
               return false;
            }
         }
      }

      rpmdbSetIteratorModified(MI, 1);

      if (changed_count != 1)
      {
         _error->Error(_("Failed to change package flags for file %s: %zu packages were found in rpmdb with such name when it was expected that exactly 1 package would be found"),
            iter->file.c_str(), changed_count);
         return false;
      }
   }

   return true;
}

#endif /* HAVE_RPM */

// vim:sts=3:sw=3
