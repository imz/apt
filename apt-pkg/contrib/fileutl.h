// Description								/*{{{*/
// $Id: fileutl.h,v 1.1 2002/07/23 17:54:51 niemeyer Exp $
/* ######################################################################

   File Utilities

   CopyFile - Buffered copy of a single file
   GetLock - dpkg compatible lock file manipulation (fcntl)
   FileExists - Returns true if the file exists
   SafeGetCWD - Returns the CWD in a string with overrun protection

   The file class is a handy abstraction for various functions+classes
   that need to accept filenames.

   This source is placed in the Public Domain, do with it what you will
   It was originally written by Jason Gunthorpe.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_FILEUTL_H
#define PKGLIB_FILEUTL_H

#include <string>
#include <vector>

using std::string;

class FileFd
{
   protected:
   int iFd;

   enum LocalFlags {AutoClose = (1<<0),Fail = (1<<1),DelOnFail = (1<<2),
                    HitEof = (1<<3)};
   unsigned long Flags;
   string FileName;

   public:
   enum OpenMode {ReadOnly,WriteEmpty,WriteExists,WriteAny,WriteTemp};

   bool Read(void *To,unsigned long Size,unsigned long *Actual = 0);
   bool Write(const void *From,unsigned long Size);
   bool Seek(unsigned long To);
   bool Skip(unsigned long To);
   bool Truncate(unsigned long To);
   unsigned long Tell();
   unsigned long Size();
   bool Open(const string &FileName,OpenMode Mode,unsigned long Perms = 0666);
   bool Close();
   bool Sync();

   // Simple manipulators
   inline int Fd() {return iFd;}
   // FIXME: get rid of Fd(), which has bad semantics: if used when already
   // owning an iFd, the old iFd is not closed (i.e., an open fd "leaks").
   inline void Fd(int fd) {iFd = fd;}
   void Reset(int const fd) {
      // close the old owned fd
      Close();
      // clear the flags and own the new fd
      Flags = AutoClose;
      iFd = fd;
   }
   int Release() {
      int const fd{iFd};
      iFd = -1;
      return fd;
   }
   inline bool IsOpen() {return iFd >= 0;}
   inline bool Failed() {return (Flags & Fail) == Fail;}
   inline void EraseOnFailure() {Flags |= DelOnFail;}
   inline void OpFail() {Flags |= Fail;}
   inline bool Eof() {return (Flags & HitEof) == HitEof;}
   inline string &Name() {return FileName;}

   // Prohibit copying; otherwise, two objects would own the fd
   // and would try to close it (on destruction). Actually,
   // -Werror=deprecated-copy -Werror=deprecated-copy-dtor should also
   // prohibit the use of the implictly-defined copy constructor and assignment
   // operator because we have a user-defined destructor (which is a hint that
   // there is some non-trivial resource management that is going on),
   // but the flags don't work in our case (with gcc10) for some reason...
   FileFd & operator= (const FileFd &) = delete;
   FileFd(const FileFd &) = delete;

   FileFd(const string &FileName,OpenMode Mode,unsigned long Perms = 0666) :
      iFd{-1},
      Flags{AutoClose}
   {
      Open(FileName,Mode,Perms);
   }
   FileFd() : iFd{-1}, Flags{AutoClose} {}
   // Taking the ownership of an fd. (We are responsible for closing it.)
   // Taking the ownership shouldn't be an implicit conversion.
   explicit FileFd(int const Fd) : iFd{Fd}, Flags{AutoClose} {}
   // Without AutoClose in Flags, Close() will not close the underlying fd.
   // What a mess! That could be an unexpected behavior. Therefore, we always
   // set AutoClose when this object is constructed or a new underlying fd
   // is set (via Open() or Reset() methods), i.e., FileFd class acts as a
   // (unique) owner of the fd. One must not use the disabled ctor below,
   // because it breaks this concept. FIXME: get rid of AutoClose in favor of
   // such default behavior of the class. TODO: make a non-owning base class for
   // FileFd (with a conversion from int fd), which could be used just for API.
   //FileFd(int Fd,bool) : iFd{Fd}, Flags{0} {}
   virtual ~FileFd();
};

bool CopyFile(FileFd &From,FileFd &To);
bool RemoveFile(const char * Function, const std::string &FileName);
bool RemoveFileAt(const char * Function, const int dirfd, const std::string &FileName);
int GetLock(const string &File,bool Errors = true);
bool FileExists(const string &File);
bool RealFileExists(const std::string &File);
bool DirectoryExists(const std::string &Path);

std::vector<std::string> GetListOfFilesInDir(const std::string &Dir, const std::string &Ext,
					bool SortList, bool AllowNoExt = false);
std::vector<std::string> GetListOfFilesInDir(const std::string &Dir, const std::vector<std::string> &Ext,
					bool SortList);
std::vector<std::string> GetListOfFilesInDir(const std::string &Dir, bool SortList);

string SafeGetCWD();
void SetCloseExec(int Fd,bool Close);
void SetNonBlock(int Fd,bool Block);
bool WaitFd(int Fd,bool write = false,unsigned long timeout = 0);
int ExecFork();
bool ExecWait(int Pid,const char *Name,bool Reap = false);

// File string manipulators
string flNotDir(const string &File);
string flNotFile(const string &File);
string flNoLink(const string &File);
string flExtension(const string &File);
string flNoExtension(const string &File);
string flUnCompressed(const string &File);
string flCombine(const string &Dir,const string &File);

#endif
