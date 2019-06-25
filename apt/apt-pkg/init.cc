// Description								/*{{{*/
// $Id: init.cc,v 1.20 2003/02/09 20:31:05 doogie Exp $
/* ######################################################################

   Init - Initialize the package library
   
   ##################################################################### */
									/*}}}*/
// Include files							/*{{{*/
#include <config.h>
#include <apt-pkg/init.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/error.h>

#include <apti18n.h>
#include <sys/stat.h>
#include <sys/utsname.h>

// CNC:2003-03-17
#include <apt-pkg/luaiface.h>

#include <cstring>
#include <cstdlib>
									/*}}}*/

#define Stringfy_(x) # x
#define Stringfy(x)  Stringfy_(x)
const char *pkgVersion = VERSION;
const char *pkgLibVersion = Stringfy(APT_PKG_MAJOR) "."
                            Stringfy(APT_PKG_MINOR) "." 
                            Stringfy(APT_PKG_RELEASE);
const char *pkgCPU = COMMON_CPU;
const char *pkgOS = COMMON_OS;
    
// pkgInitConfig - Initialize the configuration class			/*{{{*/
// ---------------------------------------------------------------------
/* Directories are specified in such a way that the FindDir function will
   understand them. That is, if they don't start with a / then their parent
   is prepended, this allows a fair degree of flexability. */
bool pkgInitConfig(Configuration &Cnf)
{
   const char *cpu = NULL;
   struct utsname name;
   if (uname(&name) == 0)
      cpu = strdup(name.machine);
   if (cpu == NULL)
      cpu = COMMON_CPU;

   // General APT things
   if (strcmp(COMMON_OS,"linux") == 0 ||
       strcmp(COMMON_OS,"unknown") == 0)
      Cnf.CndSet("APT::Architecture",cpu);
   else
      Cnf.CndSet("APT::Architecture",COMMON_OS "-" COMMON_CPU);
   // CNC:2002-09-10
   //Cnf.CndSet("APT::Build-Essential::", "build-essential");
   Cnf.CndSet("Dir","/");
   
   // State   
   Cnf.CndSet("Dir::State","var/lib/apt/");
   
   /* Just in case something goes horribly wrong, we can fall back to the
      old /var/state paths.. */
   struct stat St;   
   if (stat("/var/lib/apt/.",&St) != 0 &&
       stat("/var/state/apt/.",&St) == 0)
      Cnf.CndSet("Dir::State","var/state/apt/");
       
   Cnf.CndSet("Dir::State::lists","lists/");
   Cnf.CndSet("Dir::State::cdroms","cdroms.list");
   
   // Cache
   Cnf.CndSet("Dir::Cache","var/cache/apt/");
   Cnf.CndSet("Dir::Cache::archives","archives/");
   Cnf.CndSet("Dir::Cache::srcpkgcache","srcpkgcache.bin");
   Cnf.CndSet("Dir::Cache::pkgcache","pkgcache.bin");
   
   // Configuration
   Cnf.CndSet("Dir::Etc","etc/apt/");
   Cnf.CndSet("Dir::Etc::sourcelist","sources.list");
   // CNC:2003-03-03
   Cnf.CndSet("Dir::Etc::sourceparts","sources.list.d");
   Cnf.CndSet("Dir::Etc::vendorlist","vendors.list");
   Cnf.CndSet("Dir::Etc::vendorparts","vendors.list.d");
   Cnf.CndSet("Dir::Etc::main","apt.conf");
   Cnf.CndSet("Dir::Etc::parts","apt.conf.d");
   Cnf.CndSet("Dir::Etc::preferences","preferences");
   Cnf.CndSet("Dir::Etc::preferencesparts","preferences.d");
   Cnf.CndSet("Dir::Bin::methods",LIBDIR "/apt/methods");
   Cnf.CndSet("Acquire::ComprExtension", ".bz2");

   Cnf.Set("Dir::Ignore-Files-Silently::", "~$");
   Cnf.Set("Dir::Ignore-Files-Silently::", "\\.disabled$");
   Cnf.Set("Dir::Ignore-Files-Silently::", "\\.bak$");
   Cnf.Set("Dir::Ignore-Files-Silently::", "\\.dpkg-[a-z]+$");
   Cnf.Set("Dir::Ignore-Files-Silently::", "\\.ucf-[a-z]+$");
   Cnf.Set("Dir::Ignore-Files-Silently::", "\\.save$");
   Cnf.Set("Dir::Ignore-Files-Silently::", "\\.orig$");
   Cnf.Set("Dir::Ignore-Files-Silently::", "\\.distUpgrade$");

   bool Res = true;
   
   // Read an alternate config file
   const char *Cfg = getenv("APT_CONFIG");
   if (Cfg != 0 && FileExists(Cfg) == true)
      Res &= ReadConfigFile(Cnf,Cfg);
   
   // Read the configuration parts dir
   string Parts = Cnf.FindDir("Dir::Etc::parts");
   if (FileExists(Parts) == true)
      Res &= ReadConfigDir(Cnf,Parts);
      
   // Read the main config file
   string FName = Cnf.FindFile("Dir::Etc::main");
   if (FileExists(FName) == true)
      Res &= ReadConfigFile(Cnf,FName);
   
   if (Res == false)
      return false;
   
   if (Cnf.FindB("Debug::pkgInitConfig",false) == true)
      Cnf.Dump();
   
#ifdef APT_DOMAIN
   if (Cnf.Exists("Dir::Locale"))
   {  
      bindtextdomain(APT_DOMAIN,Cnf.FindDir("Dir::Locale").c_str());
      bindtextdomain(textdomain(0),Cnf.FindDir("Dir::Locale").c_str());
   }
#endif
   
   return true;
}
									/*}}}*/

// CNC:2003-02-16 - We must do that to force a statically linked libapt-pkg
// 		    library to include the package systems into the binary.
#include <apt-pkg/rpmsystem.h>
//#include <apt-pkg/debsystem.h>
void ForceLinkage()
{
	rpmSystem *rpmsys = &rpmSys;
	rpmsys->ArchiveSupported("");
	//debSystem *debsys = &debSys;
	//depsys->ArchiveSupported("");
}

// pkgInitSystem - Initialize the _system calss				/*{{{*/
// ---------------------------------------------------------------------
/* */
bool pkgInitSystem(Configuration &Cnf,pkgSystem *&Sys)
{
   ForceLinkage(); // CNC:2003-02-16 - See above.

   Sys = 0;
   string Label = Cnf.Find("Apt::System","");
   if (Label.empty() == false)
   {
      Sys = pkgSystem::GetSystem(Label.c_str());
      if (Sys == 0)
	 return _error->Error(_("Packaging system '%s' is not supported"),Label.c_str());
   }
   else
   {
      signed MaxScore = 0;
      for (unsigned I = 0; I != pkgSystem::GlobalListLen; I++)
      {
	 signed Score = pkgSystem::GlobalList[I]->Score(Cnf);
	 if (Score > MaxScore)
	 {
	    MaxScore = Score;
	    Sys = pkgSystem::GlobalList[I];
	 }
      }
      
      if (Sys == 0)
	 return _error->Error(_("Unable to determine a suitable system type"));
   }

   // PM:2006-02-06
   if (Cnf.Find("APT::DistroVersion", "").empty()) {
   	Cnf.Set("APT::DistroVersion", Sys->DistroVer(Cnf));
   }

   // CNC:2003-03-15
   bool Ret = Sys->Initialize(Cnf);
#ifdef WITH_LUA
   _lua->RunScripts("Scripts::Init");
#endif
   return Ret;
}
									/*}}}*/
