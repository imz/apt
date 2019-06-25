// Description								/*{{{*/
// $Id: fileutl.cc,v 1.3 2002/11/04 16:38:17 niemeyer Exp $
/* ######################################################################

   File Utilities

   CopyFile - Buffered copy of a single file
   GetLock - dpkg compatible lock file manipulation (fcntl)

   This source is placed in the Public Domain, do with it what you will
   It was originally written by Jason Gunthorpe <jgg@debian.org>.

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/configuration.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/error.h>
#include <apt-pkg/sptr.h>

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>

// CNC:2003-02-14 - Ralf Corsepius told RH8 with GCC 3.2.1 fails
//                  compiling without moving this header to here.
#include <apti18n.h>
									/*}}}*/


using namespace std;

// CopyFile - Buffered copy of a file					/*{{{*/
// ---------------------------------------------------------------------
/* The caller is expected to set things so that failure causes erasure */
bool CopyFile(FileFd &From,FileFd &To)
{
   if (From.IsOpen() == false || To.IsOpen() == false)
      return false;

   // Buffered copy between fds
   const SPtrArray<unsigned char> Buf(new unsigned char[64000]);
   unsigned long Size = From.Size();
   while (Size != 0)
   {
      unsigned long ToRead = Size;
      if (Size > 64000)
	 ToRead = 64000;

      if (From.Read(Buf.get(),ToRead) == false ||
	  To.Write(Buf.get(),ToRead) == false)
	 return false;

      Size -= ToRead;
   }

   return true;
}
									/*}}}*/
bool RemoveFileAt(const char * const Function, const int dirfd, const std::string &FileName)/*{{{*/
{
   if (FileName == "/dev/null")
      return true;

   errno = 0;

   if (unlinkat(dirfd, FileName.c_str(), 0) != 0)
   {
      if (errno == ENOENT)
         return true;

      return _error->WarningE(Function, _("Problem unlinking the file %s"), FileName.c_str());
   }

   return true;
}
									/*}}}*/
bool RemoveFile(const char * const Function, const std::string &FileName)/*{{{*/
{
   if (FileName == "/dev/null")
      return true;

   errno = 0;

   if (unlink(FileName.c_str()) != 0)
   {
      if (errno == ENOENT)
         return true;

      return _error->WarningE(Function, _("Problem unlinking the file %s"), FileName.c_str());
   }

   return true;
}
									/*}}}*/
// GetLock - Gets a lock file						/*{{{*/
// ---------------------------------------------------------------------
/* This will create an empty file of the given name and lock it. Once this
   is done all other calls to GetLock in any other process will fail with
   -1. The return result is the fd of the file, the call should call
   close at some time. */
int GetLock(const string &File,bool Errors)
{
   int FD = open(File.c_str(),O_RDWR | O_CREAT | O_TRUNC,0640);
   if (FD < 0)
   {
      // Read only .. cant have locking problems there.
      if (errno == EROFS)
      {
	 _error->Warning(_("Not using locking for read only lock file %s"),File.c_str());
	 return dup(0);       // Need something for the caller to close
      }

      if (Errors == true)
	 _error->Errno("open",_("Could not open lock file %s"),File.c_str());

      // Feh.. We do this to distinguish the lock vs open case..
      errno = EPERM;
      return -1;
   }
   SetCloseExec(FD,true);

   // Aquire a write lock
   struct flock fl;
   fl.l_type = F_WRLCK;
   fl.l_whence = SEEK_SET;
   fl.l_start = 0;
   fl.l_len = 0;
   if (fcntl(FD,F_SETLK,&fl) == -1)
   {
      if (errno == ENOLCK)
      {
	 _error->Warning(_("Not using locking for nfs mounted lock file %s"),File.c_str());
	 return dup(0);       // Need something for the caller to close
      }
      if (Errors == true)
	 _error->Errno("open",_("Could not get lock %s"),File.c_str());

      int Tmp = errno;
      close(FD);
      errno = Tmp;
      return -1;
   }

   return FD;
}
									/*}}}*/
// FileExists - Check if a file exists					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FileExists(const string &File)
{
   struct stat Buf;
   if (stat(File.c_str(),&Buf) != 0)
      return false;
   return true;
}
									/*}}}*/
// RealFileExists - Check if a file exists and if it is really a file	/*{{{*/
// ---------------------------------------------------------------------
/* */
bool RealFileExists(const string &File)
{
   struct stat Buf;
   if (stat(File.c_str(),&Buf) != 0)
      return false;
   return ((Buf.st_mode & S_IFREG) != 0);
}
									/*}}}*/
// DirectoryExists - Check if a directory exists and is really one	/*{{{*/
// ---------------------------------------------------------------------
/* */
bool DirectoryExists(const string &Path)
{
   struct stat Buf;
   if (stat(Path.c_str(),&Buf) != 0)
      return false;
   return ((Buf.st_mode & S_IFDIR) != 0);
}
									/*}}}*/
// GetListOfFilesInDir - returns a vector of files in the given dir	/*{{{*/
// ---------------------------------------------------------------------
/* If an extension is given only files with this extension are included
   in the returned vector, otherwise every "normal" file is included. */
std::vector<string> GetListOfFilesInDir(const string &Dir, const string &Ext,
					bool SortList, bool AllowNoExt)
{
   std::vector<string> ext;
   ext.reserve(2);

   if (!Ext.empty())
      ext.push_back(Ext);

   if (AllowNoExt && (!ext.empty()))
      ext.push_back("");

   return GetListOfFilesInDir(Dir, ext, SortList);
}

std::vector<string> GetListOfFilesInDir(const string &Dir, const std::vector<string> &Ext,
					bool SortList)
{
   // Attention debuggers: need to be set with the environment config file!
   const bool Debug = _config->FindB("Debug::GetListOfFilesInDir", false);
   if (Debug)
   {
      std::clog << "Accept in " << Dir << " only files with the following " << Ext.size() << " extensions:" << std::endl;
      if (Ext.empty())
         std::clog << "\tNO extension" << std::endl;
      else
         for (auto e = Ext.begin(); e != Ext.end(); ++e)
            std::clog << '\t' << (e->empty() ? "NO" : *e) << " extension" << std::endl;
   }

   std::vector<string> List;

   if (!DirectoryExists(Dir))
   {
      _error->Error(_("List of files can't be created as '%s' is not a directory"), Dir.c_str());
      return List;
   }

   Configuration::MatchAgainstConfig SilentIgnore("Dir::Ignore-Files-Silently");
   DIR *D = opendir(Dir.c_str());
   if (D == 0)
   {
      if (errno == EACCES)
         _error->WarningE("opendir", _("Unable to read %s"), Dir.c_str());
      else
         _error->Errno("opendir", _("Unable to read %s"), Dir.c_str());
      return List;
   }

   for (struct dirent *Ent = readdir(D); Ent != 0; Ent = readdir(D))
   {
      // skip "hidden" files
      if (Ent->d_name[0] == '.')
         continue;

      // Make sure it is a file and not something else
      const string File = flCombine(Dir,Ent->d_name);
#ifdef _DIRENT_HAVE_D_TYPE
      if (Ent->d_type != DT_REG)
#endif
      {
         if (!RealFileExists(File))
         {
            // do not show ignoration warnings for directories
            if (
#ifdef _DIRENT_HAVE_D_TYPE
                Ent->d_type == DT_DIR ||
#endif
                DirectoryExists(File))
               continue;
            if (!SilentIgnore.Match(Ent->d_name))
               _error->Notice(_("Ignoring '%s' in directory '%s' as it is not a regular file"), Ent->d_name, Dir.c_str());
            continue;
         }
      }

      // check for accepted extension:
      // no extension given -> periods are bad as hell!
      // extensions given -> "" extension allows no extension
      if (!Ext.empty())
      {
         string d_ext = flExtension(Ent->d_name);
         if (d_ext == Ent->d_name) // no extension
         {
            if (std::find(Ext.begin(), Ext.end(), "") == Ext.end())
            {
               if (Debug)
                  std::clog << "Bad file: " << Ent->d_name << " → no extension" << std::endl;
               if (!SilentIgnore.Match(Ent->d_name))
                  _error->Notice(_("Ignoring file '%s' in directory '%s' as it has no filename extension"), Ent->d_name, Dir.c_str());
               continue;
            }
         }
         else if (std::find(Ext.begin(), Ext.end(), d_ext) == Ext.end())
         {
            if (Debug)
               std::clog << "Bad file: " << Ent->d_name << " → bad extension »" << flExtension(Ent->d_name) << "«" << std::endl;
            if (SilentIgnore.Match(Ent->d_name) == false)
               _error->Notice(_("Ignoring file '%s' in directory '%s' as it has an invalid filename extension"), Ent->d_name, Dir.c_str());
            continue;
         }
      }

      // Skip bad filenames ala run-parts
      const char *C = Ent->d_name;
      for (; *C != 0; ++C)
         if ((!isalpha(*C)) && (!isdigit(*C))
             && (*C != '_') && (*C != '-') && (*C != ':')) {
            // no required extension -> dot is a bad character
            if ((*C == '.') && (!Ext.empty()))
               continue;
            break;
         }

      // we don't reach the end of the name -> bad character included
      if (*C != 0)
      {
         if (Debug)
            std::clog << "Bad file: " << Ent->d_name << " → bad character »"
                      << *C << "« in filename (period allowed: " << (Ext.empty() ? "no" : "yes") << ")" << std::endl;
         continue;
      }

      // skip filenames which end with a period. These are never valid
      if (*(C - 1) == '.')
      {
         if (Debug)
            std::clog << "Bad file: " << Ent->d_name << " → Period as last character" << std::endl;
         continue;
      }

      if (Debug)
         std::clog << "Accept file: " << Ent->d_name << " in " << Dir << std::endl;

      List.push_back(File);
   }

   closedir(D);

   if (SortList)
      std::sort(List.begin(),List.end());

   return List;
}

std::vector<string> GetListOfFilesInDir(const string &Dir, bool SortList)
{
   const bool Debug = _config->FindB("Debug::GetListOfFilesInDir", false);
   if (Debug)
      std::clog << "Accept in " << Dir << " all regular files" << std::endl;

   std::vector<string> List;

   if (!DirectoryExists(Dir))
   {
      _error->Error(_("List of files can't be created as '%s' is not a directory"), Dir.c_str());
      return List;
   }

   DIR *D = opendir(Dir.c_str());
   if (D == 0)
   {
      _error->Errno("opendir",_("Unable to read %s"),Dir.c_str());
      return List;
   }

   for (struct dirent *Ent = readdir(D); Ent != 0; Ent = readdir(D))
   {
      // skip "hidden" files
      if (Ent->d_name[0] == '.')
         continue;

      // Make sure it is a file and not something else
      const string File = flCombine(Dir, Ent->d_name);
#ifdef _DIRENT_HAVE_D_TYPE
      if (Ent->d_type != DT_REG)
#endif
      {
         if (!RealFileExists(File))
         {
            if (Debug)
               std::clog << "Bad file: " << Ent->d_name << " → it is not a real file" << std::endl;
            continue;
         }
      }

      // Skip bad filenames ala run-parts
      const char *C = Ent->d_name;
      for (; *C != 0; ++C)
         if ((!isalpha(*C)) && (!isdigit(*C))
             && (*C != '_') && (*C != '-') && (*C != '.'))
            break;

      // we don't reach the end of the name -> bad character included
      if (*C != 0)
      {
         if (Debug)
            std::clog << "Bad file: " << Ent->d_name << " → bad character »" << *C << "« in filename" << std::endl;
         continue;
      }

      // skip filenames which end with a period. These are never valid
      if (*(C - 1) == '.')
      {
         if (Debug)
            std::clog << "Bad file: " << Ent->d_name << " → Period as last character" << std::endl;
         continue;
      }

      if (Debug)
         std::clog << "Accept file: " << Ent->d_name << " in " << Dir << std::endl;

      List.push_back(File);
   }

   closedir(D);

   if (SortList == true)
      std::sort(List.begin(),List.end());

   return List;
}
/*}}}*/
// SafeGetCWD - This is a safer getcwd that returns a dynamic string	/*{{{*/
// ---------------------------------------------------------------------
/* We return / on failure. */
string SafeGetCWD()
{
   // Stash the current dir.
   char S[300];
   S[0] = 0;
   if (getcwd(S,sizeof(S)-2) == 0)
      return "/";
   unsigned int Len = strlen(S);
   S[Len] = '/';
   S[Len+1] = 0;
   return S;
}
									/*}}}*/
// flNotDir - Strip the directory from the filename			/*{{{*/
// ---------------------------------------------------------------------
/* */
string flNotDir(const string &File)
{
   string::size_type Res = File.rfind('/');
   if (Res == string::npos)
      return File;
   Res++;
   return string(File,Res,Res - File.length());
}
									/*}}}*/
// flNotFile - Strip the file from the directory name			/*{{{*/
// ---------------------------------------------------------------------
/* Result ends in a / */
string flNotFile(const string &File)
{
   string::size_type Res = File.rfind('/');
   if (Res == string::npos)
      return "./";
   Res++;
   return string(File,0,Res);
}
									/*}}}*/
// flExtension - Return the extension for the file or "" if none	/*{{{*/
// ---------------------------------------------------------------------
/* */
string flExtension(const string &File)
{
   string::size_type Res = File.rfind('.');
   if (Res == string::npos)
      return string();
   Res++;
   return string(File,Res,Res - File.length());
}
									/*}}}*/
// flNoLink - If file is a symlink then deref it			/*{{{*/
// ---------------------------------------------------------------------
/* If the name is not a link then the returned path is the input. */
string flNoLink(const string &File)
{
   struct stat St;
   if (lstat(File.c_str(),&St) != 0 || S_ISLNK(St.st_mode) == 0)
      return File;
   if (stat(File.c_str(),&St) != 0)
      return File;

   /* Loop resolving the link. There is no need to limit the number of
      loops because the stat call above ensures that the symlink is not
      circular */
   char Buffer[1024];
   string NFile = File;
   while (1)
   {
      // Read the link
      int Res;
      if ((Res = readlink(NFile.c_str(),Buffer,sizeof(Buffer))) <= 0 ||
	  (unsigned)Res >= sizeof(Buffer))
	  return File;

      // Append or replace the previous path
      Buffer[Res] = 0;
      if (Buffer[0] == '/')
	 NFile = Buffer;
      else
	 NFile = flNotFile(NFile) + Buffer;

      // See if we are done
      if (lstat(NFile.c_str(),&St) != 0)
	 return File;
      if (S_ISLNK(St.st_mode) == 0)
	 return NFile;
   }
}
									/*}}}*/
// flCombine - Combine a file and a directory				/*{{{*/
// ---------------------------------------------------------------------
/* If the file is an absolute path then it is just returned, otherwise
   the directory is pre-pended to it. */
string flCombine(const string &Dir, const string &File)
{
   if (File.empty() == true)
      return string();

   if (File[0] == '/' || Dir.empty() == true)
      return File;
   if (File.length() >= 2 && File[0] == '.' && File[1] == '/')
      return File;
   if (Dir[Dir.length()-1] == '/')
      return Dir + File;
   return Dir + '/' + File;
}
									/*}}}*/
// SetCloseExec - Set the close on exec flag				/*{{{*/
// ---------------------------------------------------------------------
/* */
void SetCloseExec(int Fd,bool Close)
{
   if (fcntl(Fd,F_SETFD,(Close == false)?0:FD_CLOEXEC) != 0)
   {
      cerr << "FATAL -> Could not set close on exec " << strerror(errno) << endl;
      exit(100);
   }
}
									/*}}}*/
// SetNonBlock - Set the nonblocking flag				/*{{{*/
// ---------------------------------------------------------------------
/* */
void SetNonBlock(int Fd,bool Block)
{
   int Flags = fcntl(Fd,F_GETFL) & (~O_NONBLOCK);
   if (fcntl(Fd,F_SETFL,Flags | ((Block == false)?0:O_NONBLOCK)) != 0)
   {
      cerr << "FATAL -> Could not set non-blocking flag " << strerror(errno) << endl;
      exit(100);
   }
}
									/*}}}*/
// WaitFd - Wait for a FD to become readable				/*{{{*/
// ---------------------------------------------------------------------
/* This waits for a FD to become readable using select. It is useful for
   applications making use of non-blocking sockets. The timeout is
   in seconds. */
bool WaitFd(int Fd,bool write,unsigned long timeout)
{
   fd_set Set;
   struct timeval tv;
   FD_ZERO(&Set);
   FD_SET(Fd,&Set);
   tv.tv_sec = timeout;
   tv.tv_usec = 0;
   if (write == true)
   {
      int Res;
      do
      {
	 Res = select(Fd+1,0,&Set,0,(timeout != 0?&tv:0));
      }
      while (Res < 0 && errno == EINTR);

      if (Res <= 0)
	 return false;
   }
   else
   {
      int Res;
      do
      {
	 Res = select(Fd+1,&Set,0,0,(timeout != 0?&tv:0));
      }
      while (Res < 0 && errno == EINTR);

      if (Res <= 0)
	 return false;
   }

   return true;
}
									/*}}}*/
// ExecFork - Magical fork that sanitizes the context before execing	/*{{{*/
// ---------------------------------------------------------------------
/* This is used if you want to cleanse the environment for the forked
   child, it fixes up the important signals and nukes all of the fds,
   otherwise acts like normal fork. */
int ExecFork()
{
   // Fork off the process
   pid_t Process = fork();
   if (Process < 0)
   {
      cerr << "FATAL -> Failed to fork." << endl;
      exit(100);
   }

   // Spawn the subprocess
   if (Process == 0)
   {
      // Setup the signals
      signal(SIGPIPE,SIG_DFL);
      signal(SIGQUIT,SIG_DFL);
      signal(SIGINT,SIG_DFL);
      signal(SIGWINCH,SIG_DFL);
      signal(SIGCONT,SIG_DFL);
      signal(SIGTSTP,SIG_DFL);

      // Close all of our FDs - just in case
      for (int K = 3; K != 40; K++)
	 fcntl(K,F_SETFD,FD_CLOEXEC);
   }

   return Process;
}
									/*}}}*/
// ExecWait - Fancy waitpid						/*{{{*/
// ---------------------------------------------------------------------
/* Waits for the given sub process. If Reap is set then no errors are
   generated. Otherwise a failed subprocess will generate a proper descriptive
   message */
bool ExecWait(int Pid,const char *Name,bool Reap)
{
   if (Pid <= 1)
      return true;

   // Wait and collect the error code
   int Status;
   while (waitpid(Pid,&Status,0) != Pid)
   {
      if (errno == EINTR)
	 continue;

      if (Reap == true)
	 return false;

      return _error->Error(_("Waited, for %s but it wasn't there"),Name);
   }


   // Check for an error code.
   if (WIFEXITED(Status) == 0 || WEXITSTATUS(Status) != 0)
   {
      if (Reap == true)
	 return false;
      if (WIFSIGNALED(Status) != 0)
      {
	 if( WTERMSIG(Status) == SIGSEGV)
	    return _error->Error(_("Sub-process %s received a segmentation fault."),Name);
	 else
	    return _error->Error(_("Sub-process %s received signal %u."),Name, WTERMSIG(Status));
      }

      if (WIFEXITED(Status) != 0)
	 return _error->Error(_("Sub-process %s returned an error code (%u)"),Name,WEXITSTATUS(Status));

      return _error->Error(_("Sub-process %s exited unexpectedly"),Name);
   }

   return true;
}
									/*}}}*/

// FileFd::Open - Open a file						/*{{{*/
// ---------------------------------------------------------------------
/* The most commonly used open mode combinations are given with Mode */
bool FileFd::Open(const string &FileName,OpenMode Mode, unsigned long Perms)
{
   Close();
   Flags = AutoClose;
   switch (Mode)
   {
      case ReadOnly:
      iFd = open(FileName.c_str(),O_RDONLY);
      break;

      case WriteEmpty:
      {
	 struct stat Buf;
	 if (lstat(FileName.c_str(),&Buf) == 0 && S_ISLNK(Buf.st_mode))
	    unlink(FileName.c_str());
	 iFd = open(FileName.c_str(),O_RDWR | O_CREAT | O_TRUNC,Perms);
	 break;
      }

      case WriteExists:
      iFd = open(FileName.c_str(),O_RDWR);
      break;

      case WriteAny:
      iFd = open(FileName.c_str(),O_RDWR | O_CREAT,Perms);
      break;

      case WriteTemp:
      unlink(FileName.c_str());
      iFd = open(FileName.c_str(),O_RDWR | O_CREAT | O_EXCL,Perms);
      break;
   }

   if (iFd < 0)
      return _error->Errno("open",_("Could not open file %s"),FileName.c_str());

   this->FileName = FileName;
   SetCloseExec(iFd,true);
   return true;
}
									/*}}}*/
// FileFd::~File - Closes the file					/*{{{*/
// ---------------------------------------------------------------------
/* If the proper modes are selected then we close the Fd and possibly
   unlink the file on error. */
FileFd::~FileFd()
{
   Close();
}
									/*}}}*/
// FileFd::Read - Read a bit of the file				/*{{{*/
// ---------------------------------------------------------------------
/* We are carefull to handle interruption by a signal while reading
   gracefully. */
bool FileFd::Read(void *To,unsigned long Size,unsigned long *Actual)
{
   int Res;
   errno = 0;
   if (Actual != 0)
      *Actual = 0;

   do
   {
      Res = read(iFd,To,Size);
      if (Res < 0 && errno == EINTR)
	 continue;
      if (Res < 0)
      {
	 Flags |= Fail;
	 return _error->Errno("read",_("Read error"));
      }

      To = (char *)To + Res;
      Size -= Res;
      if (Actual != 0)
	 *Actual += Res;
   }
   while (Res > 0 && Size > 0);

   if (Size == 0)
      return true;

   // Eof handling
   if (Actual != 0)
   {
      Flags |= HitEof;
      return true;
   }

   Flags |= Fail;
   return _error->Error(_("read, still have %lu to read but none left"),Size);
}
									/*}}}*/
// FileFd::Write - Write to the file					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FileFd::Write(const void *From,unsigned long Size)
{
   int Res;
   errno = 0;
   do
   {
      Res = write(iFd,From,Size);
      if (Res < 0 && errno == EINTR)
	 continue;
      if (Res < 0)
      {
	 Flags |= Fail;
	 return _error->Errno("write",_("Write error"));
      }

      From = (char *)From + Res;
      Size -= Res;
   }
   while (Res > 0 && Size > 0);

   if (Size == 0)
      return true;

   Flags |= Fail;
   return _error->Error(_("write, still have %lu to write but couldn't"),Size);
}
									/*}}}*/
// FileFd::Seek - Seek in the file					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FileFd::Seek(unsigned long To)
{
   if (lseek(iFd,To,SEEK_SET) != (signed)To)
   {
      Flags |= Fail;
      return _error->Error("Unable to seek to %lu",To);
   }

   return true;
}
									/*}}}*/
// FileFd::Skip - Seek in the file					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FileFd::Skip(unsigned long Over)
{
   if (lseek(iFd,Over,SEEK_CUR) < 0)
   {
      Flags |= Fail;
      return _error->Error("Unable to seek ahead %lu",Over);
   }

   return true;
}
									/*}}}*/
// FileFd::Truncate - Truncate the file 				/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FileFd::Truncate(unsigned long To)
{
   if (ftruncate(iFd,To) != 0)
   {
      Flags |= Fail;
      return _error->Error("Unable to truncate to %lu",To);
   }

   return true;
}
									/*}}}*/
// FileFd::Tell - Current seek position					/*{{{*/
// ---------------------------------------------------------------------
/* */
unsigned long FileFd::Tell()
{
   off_t Res = lseek(iFd,0,SEEK_CUR);
   if (Res == (off_t)-1)
      _error->Errno("lseek","Failed to determine the current file position");
   return Res;
}
									/*}}}*/
// FileFd::Size - Return the size of the file				/*{{{*/
// ---------------------------------------------------------------------
/* */
unsigned long FileFd::Size()
{
   struct stat Buf;
   if (fstat(iFd,&Buf) != 0)
      return _error->Errno("fstat","Unable to determine the file size");
   return Buf.st_size;
}
									/*}}}*/
// FileFd::Close - Close the file if the close flag is set		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FileFd::Close()
{
   bool Res = true;
   if ((Flags & AutoClose) == AutoClose)
      if (iFd >= 0 && close(iFd) != 0)
	 Res &= _error->Errno("close",_("Problem closing the file"));
   iFd = -1;

   if ((Flags & Fail) == Fail && (Flags & DelOnFail) == DelOnFail &&
       FileName.empty() == false)
      if (unlink(FileName.c_str()) != 0)
	 Res &= _error->WarningE("unlnk",_("Problem unlinking the file"));
   return Res;
}
									/*}}}*/
// FileFd::Sync - Sync the file						/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FileFd::Sync()
{
#ifdef _POSIX_SYNCHRONIZED_IO
   if (fsync(iFd) != 0)
      return _error->Errno("sync",_("Problem syncing the file"));
#endif
   return true;
}
									/*}}}*/
