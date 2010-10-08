
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

   unsigned int iOffset;
   unsigned int iSize;
   Header HeaderP;
   string ID;

   public:

   // Return a unique ID for that handler. Actually, implemented used
   // the file/dir name.
   virtual string GetID() { return ID; }

   virtual bool Skip() = 0;
   virtual bool Jump(unsigned int Offset) = 0;
   virtual void Rewind() = 0;
   inline unsigned Offset() const {return iOffset;}
   virtual bool OrderedOffset() const {return true;}
   inline unsigned Size() const {return iSize;}
   inline Header GetHeader() const {return HeaderP;}
   virtual bool IsDatabase() const = 0;

   virtual string FileName() const {return "";}
   virtual string Directory() const {return "";}
   virtual unsigned long FileSize() const {return 1;}
   virtual string MD5Sum() const {return "";}
   virtual bool ProvideFileName() const {return false;}

   RPMHandler() : iOffset(0), iSize(0), HeaderP(0) {}
   virtual ~RPMHandler() {}
};


class RPMFileHandler : public RPMHandler
{
   protected:

   FD_t FD;

   public:

   virtual bool Skip() override;
   virtual bool Jump(unsigned int Offset) override;
   virtual void Rewind() override;
   virtual inline bool IsDatabase() const override {return false;}

   virtual string FileName() const override;
   virtual string Directory() const override;
   virtual unsigned long FileSize() const override;
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
   virtual bool Jump(unsigned int Offset) override;
   virtual void Rewind() override;

   virtual string FileName() const override {return sFilePath;}
   virtual string Directory() const override {return "";}
   virtual unsigned long FileSize() const override;
   virtual string MD5Sum() const override;
   virtual bool ProvideFileName() const override {return true;}

   RPMSingleFileHandler(string File) : RPMFileHandler(File), sFilePath(File) {}
   virtual ~RPMSingleFileHandler() {}
};



class RPMDBHandler : public RPMHandler
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
   virtual bool Jump(unsigned Offset) override;
   virtual void Rewind() override;
   virtual inline bool IsDatabase() const override {return true;}
   virtual bool HasWriteLock() const {return WriteLock;}
   virtual time_t Mtime() const {return DbFileMtime;}
   virtual unsigned long Mnanotime() const {return DbFileMnanotime;}
   virtual bool OrderedOffset() const override {return false;}

   RPMDBHandler(bool WriteLock=false);
   virtual ~RPMDBHandler();
};

class RPMDirHandler : public RPMHandler
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
   virtual bool Jump(unsigned int Offset) override;
   virtual void Rewind() override;
   virtual inline bool IsDatabase() const override {return false;}

   virtual string FileName() const override {return (Dir == NULL)?"":sFileName;}
   virtual unsigned long FileSize() const override;
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
