
/* ######################################################################

   RPM database and hdlist related handling

   ######################################################################
 */


#ifndef PKGLIB_RPMHANDLER_H
#define PKGLIB_RPMHANDLER_H

#include <apt-pkg/fileutl.h>

#include <rpm/rpmlib.h>
#include <rpm/rpmmacro.h>
#include "rapttypes.h"

#include <sys/types.h>
#include <dirent.h>

#include <vector>

// Our Extra RPM tags. These should not be accessed directly. Use
// the methods in RPMHandler instead.
#define CRPMTAG_FILENAME          1000000
#define CRPMTAG_FILESIZE          1000001
#define CRPMTAG_MD5               1000005
#define CRPMTAG_SHA1              1000006
#define CRPMTAG_BLAKE2B           1000009

#define CRPMTAG_DIRECTORY         1000010
#define CRPMTAG_BINARY            1000011

struct Dependency
{
   string Name;
   string Version;
   unsigned int Op;
   unsigned int Type;
};

class RPMHandler
{
   protected:

   off_t iOffset;
   off_t iSize;
   string ID;

   unsigned int DepOp(raptDepFlags rpmflags) const;
   bool InternalDep(const char *name, const char *ver, raptDepFlags flag) const;
   bool PutDep(const char *name, const char *ver, raptDepFlags flags,
	       unsigned int type, bool checkInternalDep,
	       std::vector<Dependency*> &Deps) const;

   public:

   // Return a unique ID for that handler. Actually, implemented used
   // the file/dir name.
   virtual string GetID() { return ID; }

   virtual bool Skip() = 0;
   virtual bool Jump(off_t Offset) = 0;
   virtual void Rewind() = 0;
   inline off_t Offset() const {return iOffset;}
   virtual bool OrderedOffset() const {return true;}
   inline off_t Size() const {return iSize;}
   virtual bool IsDatabase() const {return false;}

   virtual string FileName() const = 0;
   virtual string Directory() const = 0;
   virtual off_t FileSize() const = 0;
   virtual string MD5Sum() const = 0;
   virtual string BLAKE2b() const = 0;
   virtual bool ProvideFileName() const {return false;}

   virtual string Name() const = 0;
   virtual string Arch() const = 0;
   virtual string Version() const = 0;
   virtual string EVRDB() const = 0;
   virtual string Group() const = 0;
   virtual string Packager() const = 0;
   virtual string Summary() const = 0;
   virtual string Description() const = 0;
   virtual bool AutoInstalled() const = 0;
   virtual off_t InstalledSize() const = 0;
   virtual string SourceRpm() const = 0;

   virtual bool PRCO(unsigned int Type, std::vector<Dependency*> &Deps,
                     bool checkInternalDep) const = 0;
   /* a virtual method with default parameter is confusing; instead, define: */
   bool PRCO(const unsigned int Type, std::vector<Dependency*> &Deps) const
   { return PRCO(Type,Deps,true); }
   virtual bool FileList(std::vector<string> &FileList) const = 0;
   virtual string Changelog() const = 0;

   virtual bool HasFile(const char *File) const;

   RPMHandler() : iOffset(0), iSize(0) {}
   virtual ~RPMHandler() {}
};

class RPMHdrHandler : public RPMHandler
{
   protected:

   Header HeaderP;

   string GetSTag(raptTag Tag) const;
   off_t GetITag(raptTag Tag) const;

   public:

   virtual string FileName() const override {return "";}
   virtual string Directory() const override {return "";}
   virtual off_t FileSize() const override {return 1;}

   virtual string Name() const override {return GetSTag(RPMTAG_NAME);}
   virtual string Arch() const override {return GetSTag(RPMTAG_ARCH);}
   virtual string Version() const override {return GetSTag(RPMTAG_VERSION);}
   virtual string EVRDB() const override;
   virtual string Group() const override {return GetSTag(RPMTAG_GROUP);}
   virtual string Packager() const override;
   virtual string Summary() const override {return GetSTag(RPMTAG_SUMMARY);}
   virtual string Description() const override {return GetSTag(RPMTAG_DESCRIPTION);}
   virtual bool AutoInstalled() const override {return GetITag(RPMTAG_AUTOINSTALLED);}
   virtual off_t InstalledSize() const override {return GetITag(RPMTAG_SIZE);}
   virtual string SourceRpm() const override {return GetSTag(RPMTAG_SOURCERPM);}

   virtual bool PRCO(unsigned int Type, std::vector<Dependency*> &Deps,
                     bool checkInternalDep) const override;
   virtual bool FileList(std::vector<string> &FileList) const override;
   virtual string Changelog() const override;

   RPMHdrHandler() : RPMHandler(), HeaderP(0) {}
   virtual ~RPMHdrHandler() {}
};


class RPMFileHandler : public RPMHdrHandler
{
   protected:

   FD_t FD;

   public:

   virtual bool Skip() override;
   virtual bool Jump(off_t Offset) override;
   virtual void Rewind() override;

   virtual string FileName() const override {return GetSTag(CRPMTAG_FILENAME);}
   virtual string Directory() const override {return GetSTag(CRPMTAG_DIRECTORY);}
   virtual off_t FileSize() const override {return GetITag(CRPMTAG_FILESIZE);}
   virtual string MD5Sum() const override {return GetSTag(CRPMTAG_MD5);}
   virtual string BLAKE2b() const override {return GetSTag(CRPMTAG_BLAKE2B);}

   RPMFileHandler(FileFd *File);
   RPMFileHandler(const string &File);
   virtual ~RPMFileHandler();
};

class RPMSingleFileHandler : public RPMFileHandler
{
   protected:

   string sFilePath;

   public:

   virtual bool Skip() override;
   virtual bool Jump(off_t Offset) override;
   virtual void Rewind() override;

   virtual string FileName() const override {return sFilePath;}
   virtual string Directory() const override {return "";}
   virtual off_t FileSize() const override;
   virtual string MD5Sum() const override;
   virtual string BLAKE2b() const override;
   virtual bool ProvideFileName() const override {return true;}

   RPMSingleFileHandler(const string &File) : RPMFileHandler(File), sFilePath(File) {}
   virtual ~RPMSingleFileHandler() {}
};

class RPMDBHandler : public RPMHdrHandler
{
   protected:

   rpmts Handler;
   rpmdbMatchIterator RpmIter;
   bool WriteLock;

   time_t DbFileMtime;
   unsigned long DbFileMnanotime;

   // not available in the DB (probably, because it's for the original file)
   virtual string MD5Sum() const override {return "";}
   virtual string BLAKE2b() const override {return "";}

   public:

   static string DataPath(bool DirectoryOnly=true);
   virtual bool Skip() override;
   virtual bool Jump(off_t Offset) override;
   virtual void Rewind() override;
   virtual bool IsDatabase() const override {return true;}
   virtual bool HasWriteLock() const {return WriteLock;}
   virtual time_t Mtime() const {return DbFileMtime;}
   virtual unsigned long Mnanotime() const {return DbFileMnanotime;}
   virtual bool OrderedOffset() const override {return false;}

   // used by rpmSystem::DistroVer()
   bool JumpByName(const string &PkgName, bool Provides=false);

   RPMDBHandler(bool WriteLock=false);
   virtual ~RPMDBHandler();
};

class RPMDirHandler : public RPMHdrHandler
{
   protected:

   DIR *Dir;
   string sDirName;
   string sFileName;
   string sFilePath;

   rpmts TS;

   const char *nextFileName();

   public:

   virtual bool Skip() override;
   virtual bool Jump(off_t Offset) override;
   virtual void Rewind() override;

   virtual string FileName() const override {return (Dir == NULL)?"":sFileName;}
   virtual off_t FileSize() const override;
   virtual string MD5Sum() const override;
   virtual string BLAKE2b() const override;

   RPMDirHandler(const string &DirName);
   virtual ~RPMDirHandler();
};

#endif
// vim:sts=3:sw=3
