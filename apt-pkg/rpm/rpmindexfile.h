// Description								/*{{{*/
// $Id $
/* ######################################################################

   RPM Index Files

   There are three sorts currently

   pkglist files
   The system RPM database
   srclist files

   #####################################################################
 */
									/*}}}*/
#ifndef PKGLIB_RPMINDEXFILE_H
#define PKGLIB_RPMINDEXFILE_H

#include <apt-pkg/indexfile.h>
#include <apt-pkg/rpmhandler.h>

class RPMHandler;
class RPMDBHandler;
class pkgRepository;

class rpmIndexFile : public pkgIndexFile
{

   public:

   virtual RPMHandler *CreateHandler() const = 0;
   virtual bool HasPackages() const override {return false;}

};

class rpmDatabaseIndex : public rpmIndexFile
{
   public:

   virtual const Type *GetType() const override;

   // Creates a RPMHandler suitable for usage with this object
   virtual RPMHandler *CreateHandler() const override;

   // Interface for acquire
   virtual string Describe(bool Short) const override {return "RPM Database";}

   // Interface for the Cache Generator
   virtual bool Exists() const override {return true;}
   virtual unsigned long Size() const override;
   virtual bool HasPackages() const override {return true;}
   virtual bool Merge(pkgCacheGenerator &Gen,OpProgress &Prog) const override;
   virtual bool MergeFileProvides(pkgCacheGenerator &/*Gen*/,
				  OpProgress &/*Prog*/) const override;
   virtual pkgCache::PkgFileIterator FindInCache(pkgCache &Cache) const override;

   rpmDatabaseIndex();
};

class rpmListIndex : public rpmIndexFile
{

   protected:

   string URI;
   string Dist;
   string Section;
   pkgRepository *Repository;

   string ReleaseFile(string Type) const;
   string ReleaseURI(string Type) const;
   string ReleaseInfo(string Type) const;

   string Info(string Type) const;
   string IndexFile(string Type) const;
   string IndexURI(string Type) const;

   virtual string MainType() const = 0;
   virtual string IndexPath() const {return IndexFile(MainType());}
   virtual string ReleasePath() const {return IndexFile("release");}

   public:

   virtual bool GetReleases(pkgAcquire *Owner) const override;

   // Interface for the Cache Generator
   virtual bool Exists() const override;
   virtual unsigned long Size() const override;

   // Interface for acquire
   virtual string Describe(bool Short) const override;

   rpmListIndex(string URI,string Dist,string Section,
		pkgRepository *Repository) :
		URI(URI), Dist(Dist), Section(Section),
		Repository(Repository)
	{}
};

class rpmPkgListIndex : public rpmListIndex
{
   protected:

   virtual string MainType() const override {return "pkglist";}

   public:

   virtual const Type *GetType() const override;

   // Creates a RPMHandler suitable for usage with this object
   virtual RPMHandler *CreateHandler() const override
	   { return new RPMFileHandler(IndexPath()); }

   // Stuff for accessing files on remote items
   virtual string ArchiveInfo(pkgCache::VerIterator Ver) const override;
   virtual string ArchiveURI(string File) const override;

   // Interface for acquire
   virtual bool GetIndexes(pkgAcquire *Owner) const override;

   // Interface for the Cache Generator
   virtual bool HasPackages() const override {return true;}
   virtual bool Merge(pkgCacheGenerator &Gen,OpProgress &Prog) const override;
   virtual bool MergeFileProvides(pkgCacheGenerator &/*Gen*/,
				  OpProgress &/*Prog*/) const override;
   virtual pkgCache::PkgFileIterator FindInCache(pkgCache &Cache) const override;

   rpmPkgListIndex(string URI,string Dist,string Section,
		   pkgRepository *Repository) :
	   rpmListIndex(URI,Dist,Section,Repository)
      {}
};


class rpmSrcListIndex : public rpmListIndex
{
   protected:

   virtual string MainType() const override {return "srclist";}

   public:

   virtual const Type *GetType() const override;

   // Creates a RPMHandler suitable for usage with this object
   virtual RPMHandler *CreateHandler() const override
	   { return new RPMFileHandler(IndexPath()); }

   // Stuff for accessing files on remote items
   virtual string SourceInfo(pkgSrcRecords::Parser const &Record,
			     pkgSrcRecords::File const &File) const override;
   virtual string ArchiveURI(string File) const override;

   // Interface for acquire
   virtual bool GetIndexes(pkgAcquire *Owner) const override;

   // Interface for the record parsers
   virtual pkgSrcRecords::Parser *CreateSrcParser() const override;


   rpmSrcListIndex(string URI,string Dist,string Section,
		   pkgRepository *Repository) :
	   rpmListIndex(URI,Dist,Section,Repository)
      {}
};

class rpmPkgDirIndex : public rpmPkgListIndex
{
   protected:

   virtual string MainType() const override {return "pkgdir";}
   virtual string IndexPath() const override;
   virtual string ReleasePath() const override;

   public:

   virtual bool GetReleases(pkgAcquire *Owner) const override { return true; }
   virtual bool GetIndexes(pkgAcquire *Owner) const override { return true; }

   // Creates a RPMHandler suitable for usage with this object
   virtual RPMHandler *CreateHandler() const override
	   { return new RPMDirHandler(IndexPath()); }

   virtual const Type *GetType() const override;

   // Interface for the Cache Generator
   virtual unsigned long Size() const override;

   rpmPkgDirIndex(string URI,string Dist,string Section,
		   pkgRepository *Repository) :
	   rpmPkgListIndex(URI,Dist,Section,Repository)
      {}
};

class rpmSrcDirIndex : public rpmSrcListIndex
{
   protected:

   virtual string MainType() const override {return "srcdir";}
   virtual string IndexPath() const override;

   public:

   virtual bool GetReleases(pkgAcquire *Owner) const override { return true; }
   virtual bool GetIndexes(pkgAcquire *Owner) const override { return true; }

   // Creates a RPMHandler suitable for usage with this object
   virtual RPMHandler *CreateHandler() const override
	   { return new RPMDirHandler(IndexPath()); }

   virtual const Type *GetType() const override;

   // Interface for the Cache Generator
   virtual unsigned long Size() const override;

   rpmSrcDirIndex(string URI,string Dist,string Section,
		   pkgRepository *Repository) :
	   rpmSrcListIndex(URI,Dist,Section,Repository)
      {}
};

class rpmSinglePkgIndex : public rpmPkgListIndex
{
   protected:

   string FilePath;

   virtual string MainType() const override {return "pkg";}
   virtual string IndexPath() const override {return FilePath;}

   public:

   virtual bool GetReleases(pkgAcquire *Owner) const override { return true; }
   virtual bool GetIndexes(pkgAcquire *Owner) const override { return true; }

   // Creates a RPMHandler suitable for usage with this object
   virtual RPMHandler *CreateHandler() const override
	   { return new RPMSingleFileHandler(IndexPath()); }

   virtual string ArchiveURI(string File) const override;

   virtual const Type *GetType() const override;

   rpmSinglePkgIndex(string File) :
	   rpmPkgListIndex("", "", "", NULL), FilePath(File) {}
};

class rpmSingleSrcIndex : public rpmSrcListIndex
{
   protected:

   string FilePath;

   virtual string MainType() const override {return "src";}
   virtual string IndexPath() const override {return FilePath;}

   public:

   virtual bool GetReleases(pkgAcquire *Owner) const override { return true; }
   virtual bool GetIndexes(pkgAcquire *Owner) const override { return true; }

   // Creates a RPMHandler suitable for usage with this object
   virtual RPMHandler *CreateHandler() const override
	   { return new RPMSingleFileHandler(IndexPath()); }

   virtual string ArchiveURI(string File) const override;

   virtual const Type *GetType() const override;

   rpmSingleSrcIndex(string File) :
	   rpmSrcListIndex("", "", "", NULL), FilePath(File) {}
};

#endif
