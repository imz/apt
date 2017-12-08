/* #####################################################################
   apt-mark - show and change auto-installed bit information
   ##################################################################### */

#include <config.h>

#include <apt-pkg/cachefile.h>
#include <apt-pkg/cmndline.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/init.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/cacheiterators.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/depcache.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/packagemanager.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

#include <apti18n.h>

std::ostream c0out(0);
std::ostream c1out(0);
std::ostream c2out(0);
std::ostream c3out(0); // script output stream
std::ofstream devnull("/dev/null");
unsigned int ScreenWidth = 80;

/* DoAuto - mark packages as automatically/manually installed */
static bool DoAuto(CommandLine &CmdL)
{
   CacheFile Cache(c1out);
   if (!Cache.OpenForInstall())
   {
      return false;
   }

   pkgDepCache * const DepCache = static_cast<pkgDepCache*>(Cache);
   if (DepCache == nullptr)
   {
      return false;
   }

   int AutoMarkChanged = 0;

   if (!pkgDoAuto(c1out, CmdL, AutoMarkChanged, *DepCache))
   {
      return false;
   }

   if ((AutoMarkChanged > 0) && (!_config->FindB("APT::Mark::Simulate", false)))
   {
      std::unique_ptr<pkgPackageManager> PM(_system->CreatePM(Cache));
      _system->UnLock();
      return PM->UpdateMarks();
   }

   return true;
}

/* ShowAuto - show automatically installed packages (sorted) */
static bool ShowAuto(CommandLine &CmdL)
{
   CacheFile Cache(c1out);
   if (!Cache.Open(false))
   {
      return false;
   }

   pkgDepCache * const DepCache = static_cast<pkgDepCache*>(Cache);
   if (DepCache == nullptr)
   {
      return false;
   }

   if (!pkgDoShowAuto(std::cout, CmdL, *DepCache))
   {
      return false;
   }

   return true;
}

static bool ShowHelp()
{
   std::cout <<
      _("Usage: apt-mark [options] {auto|manual} pkg1 [pkg2 ...]\n"
      "\n"
      "apt-mark is a simple command line interface for marking packages\n"
      "as manually or automatically installed.\n");

   std::cout <<
      _("Usage: apt-mark [options] command\n"
      "\n"
      "apt-mark is a simple command line interface for marking packages\n"
      "as manually or automatically installed.\n"
      "\n"
      "Commands:\n"
      "\tauto pkg1 [pkg2 ...] - mark the given packages as automatically installed\n"
      "\tmanual pkg1 [pkg2 ...] - mark the given packages as manually installed\n"
      "\tshowauto [pkg1 ...] - print the list of automatically installed packages\n"
      "\tshowmanual [pkg1 ...] - Print the list of manually installed packages\n"
      "\tshowstate [pkg1 ...] - Print list of packages and their states (auto/manual)\n");

   return true;
}

int main(int argc, char **argv)
{
   CommandLine::Args Args[] = {
      {'h',"help","help",0},
      {'v',"version","version",0},
      {'q',"quiet","quiet",CommandLine::IntLevel},
      { 0, "simple-output","simple-output",0},
      {'s',"simulate","APT::Mark::Simulate",0},
      {'s',"dry-run","APT::Mark::Simulate",0},
      {0,0,0,0}
   };

   CommandLine::Dispatch Cmds[] = {
      {"auto",&DoAuto},
      {"manual",&DoAuto},
      {"showauto",&ShowAuto},
      {"showmanual",&ShowAuto},
      {"showstate", &ShowAuto},
      {0,0}
   };

   // Set up gettext support
   setlocale(LC_ALL,"");
   textdomain(PACKAGE);

   // Parse the command line and initialize the package library
   CommandLine CmdL(Args,_config);
   if ((!pkgInitConfig(*_config))
      || (!pkgInitSystem(*_config, _system))
      || (!CmdL.Parse(argc, const_cast<const char**>(argv))))
   {
      _error->DumpErrors();
      return 100;
   }

   // See if the help should be shown
   if (_config->FindB("help")
      || (CmdL.FileSize() == 0))
   {
      return ShowHelp();
   }

   // Deal with stdout not being a tty
   if (ttyname(STDOUT_FILENO) == 0 && _config->FindI("quiet",0) < 1)
   {
      _config->Set("quiet","1");
   }

   // Setup the output streams
   if (_config->FindB("simple-output"))
   {
      c0out.rdbuf(devnull.rdbuf());
      c1out.rdbuf(devnull.rdbuf());
      c2out.rdbuf(devnull.rdbuf());
      c3out.rdbuf(std::cout.rdbuf());
   }
   else
   {
      c0out.rdbuf(std::cout.rdbuf());
      c1out.rdbuf(std::cout.rdbuf());
      c2out.rdbuf(std::cout.rdbuf());
      c3out.rdbuf(devnull.rdbuf());
   }

   if (_config->FindI("quiet",0) > 0)
   {
      c0out.rdbuf(devnull.rdbuf());
   }

   if (_config->FindI("quiet",0) > 1)
   {
      c1out.rdbuf(devnull.rdbuf());
   }

   // Match the operation
   CmdL.DispatchArg(Cmds);

   // Print any errors or warnings found during parsing
   if (!_error->empty())
   {
      bool Errors = _error->PendingError();
      _error->DumpErrors();
      if (Errors)
      {
          return 100;
      }
   }

   return 0;
}
