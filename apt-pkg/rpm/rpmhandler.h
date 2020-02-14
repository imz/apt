
/* ######################################################################

   RPM database and hdlist related handling

   ######################################################################
 */


#ifndef PKGLIB_RPMHANDLER_H
#define PKGLIB_RPMHANDLER_H

#include <sys/types.h>
#include <dirent.h>

#include <apt-pkg/fileutl.h>

#include <rpm/rpmlib.h>
#include <rpm/rpmmacro.h>

#include "rapttypes.h"

class RPMHandler
{
   protected:

   off_t iOffset;
   off_t iSize;
   string ID;

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
   virtual Header GetHeader() const = 0;

   virtual string FileName() const = 0;
   virtual string Directory() const = 0;
   virtual off_t FileSize() const = 0;
   virtual string MD5Sum() const = 0;
   virtual bool ProvideFileName() const {return false;}

   virtual string Name() const = 0;
   virtual string Arch() const = 0;
   virtual string Version() const = 0;
   virtual string EVRDB() const = 0;
   virtual string Group() const = 0;
   virtual string Maintainer() const = 0;

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

   virtual Header GetHeader() const override {return HeaderP;}
   virtual string FileName() const override {return "";}
   virtual string Directory() const override {return "";}
   virtual off_t FileSize() const override {return 1;}
   virtual string MD5Sum() const override {return "";}

   virtual string Name() const override {return GetSTag(RPMTAG_NAME);}
   virtual string Arch() const override {return GetSTag(RPMTAG_ARCH);}
   virtual string Version() const override {return GetSTag(RPMTAG_VERSION);}
   virtual string EVRDB() const override;
   virtual string Group() const override {return GetSTag(RPMTAG_GROUP);}
   virtual string Maintainer() const override;

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

   virtual string FileName() const override;
   virtual string Directory() const override;
   virtual off_t FileSize() const override;
   virtual string MD5Sum() const override;

   RPMFileHandler(FileFd *File);
   RPMFileHandler(string File);
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
   virtual bool ProvideFileName() const override {return true;}

   RPMSingleFileHandler(string File) : RPMFileHandler(File), sFilePath(File) {}
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

   RPMDirHandler(string DirName);
   virtual ~RPMDirHandler();
};


// Our Extra RPM tags. These should not be accessed directly. Use
// the methods in RPMHandler instead.
#define CRPMTAG_FILENAME          1000000
#define CRPMTAG_FILESIZE          1000001
#define CRPMTAG_MD5               1000005
#define CRPMTAG_SHA1              1000006

#define CRPMTAG_DIRECTORY         1000010
#define CRPMTAG_BINARY            1000011

#define CRPMTAG_UPDATE_SUMMARY    1000020
#define CRPMTAG_UPDATE_IMPORTANCE 1000021
#define CRPMTAG_UPDATE_DATE       1000022
#define CRPMTAG_UPDATE_URL        1000023

#endif
