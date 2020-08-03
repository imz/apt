// Description								/*{{{*/
// $Id: pkgcachegen.h,v 1.3 2002/07/25 18:07:18 niemeyer Exp $
/* ######################################################################

   Package Cache Generator - Generator for the cache structure.

   This builds the cache structure from the abstract package list parser.
   Each archive source has it's own list parser that is instantiated by
   the caller to provide data for the generator.

   Parts of the cache are created by this generator class while other
   parts are created by the list parser. The list parser is responsible
   for creating version, depends and provides structures, and some of
   their contents

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_PKGCACHEGEN_H
#define PKGLIB_PKGCACHEGEN_H

#include <apt-pkg/pkgcache.h>
#include <memory>

#include <optional>

class pkgSourceList;
class OpProgress;
class MMap;
class pkgIndexFile;

class pkgCacheGenerator
{
   private:

   pkgCache::StringItem *UniqHash[26];
   std::optional<unsigned long> WriteStringInMap(const std::string &String) { return WriteStringInMap(String.c_str(), String.length()); }
   std::optional<unsigned long> WriteStringInMap(const char *String);
   std::optional<unsigned long> WriteStringInMap(const char *String, unsigned long Len);
   std::optional<unsigned long> AllocateInMap(unsigned long size);

   public:

   class ListParser;
   friend class ListParser;

   protected:

   DynamicMMap &Map;
   pkgCache Cache;
   OpProgress *Progress;

   string PkgFileName;
   pkgCache::PackageFile *CurrentFile;

   // Flag file dependencies
   bool FoundFileDeps;

   bool NewFileVer(pkgCache::VerIterator &Ver,ListParser &List);
   std::optional<unsigned long> NewVersion(pkgCache::VerIterator &Ver,const string &VerStr,unsigned long Next);

   public:

   // CNC:2003-02-27 - We need this in rpmListParser.
   bool NewPackage(pkgCache::PkgIterator &PkgI,const string &Pkg);

   std::optional<unsigned long> WriteUniqString(const char *S,unsigned int Size);
   inline std::optional<unsigned long> WriteUniqString(const string &S) {return WriteUniqString(S.c_str(),S.length());}

   void DropProgress() {Progress = 0;}
   bool SelectFile(const string &File,const string &Site,pkgIndexFile const &Index,
		   unsigned long Flags = 0);
   bool MergeList(ListParser &List,pkgCache::VerIterator *Ver = 0);
   inline pkgCache &GetCache() {return Cache;}
   inline pkgCache::PkgFileIterator GetCurFile()
         {return pkgCache::PkgFileIterator(Cache,CurrentFile);}

   bool HasFileDeps() {return FoundFileDeps;}
   bool MergeFileProvides(ListParser &List);

   // CNC:2003-03-18
   inline void ResetFileDeps() {FoundFileDeps = false;}

   pkgCacheGenerator(DynamicMMap &aMap,OpProgress *Progress);
   ~pkgCacheGenerator();
};

// This is the abstract package list parser class.
class pkgCacheGenerator::ListParser
{
   // Some cache items
   pkgCache::VerIterator OldDepVer;
   map_ptrloc *OldDepLast;

   // Flag file dependencies
   bool FoundFileDeps;

   protected:

   // CNC:2003-02-27 - We need Owner in rpmListParser.
   pkgCacheGenerator *Owner;
   friend class pkgCacheGenerator;

   inline std::optional<unsigned long> WriteUniqString(const string &S) {return Owner->WriteUniqString(S);}
   inline std::optional<unsigned long> WriteUniqString(const char *S,unsigned int Size) {return Owner->WriteUniqString(S,Size);}
   inline std::optional<unsigned long> WriteString(const string &S) {return Owner->WriteStringInMap(S);}
   inline std::optional<unsigned long> WriteString(const char *S,unsigned int Size) {return Owner->WriteStringInMap(S,Size);}
   bool NewDepends(pkgCache::VerIterator &Ver, const string &Package,
		   const string &Version,unsigned int Op,
		   unsigned int Type);
   bool NewProvides(pkgCache::VerIterator &Ver,const string &Package, const string &Version);

   public:

   // These all operate against the current section
   virtual string Package() = 0;
   virtual string Version() = 0;
   // CNC:2002-07-09
   virtual string Architecture() {return string();}
   virtual bool NewVersion(pkgCache::VerIterator &Ver) = 0;
   virtual unsigned short VersionHash() = 0;
   virtual bool UsePackage(pkgCache::PkgIterator &Pkg,
			   pkgCache::VerIterator &Ver) = 0;
   virtual unsigned long Offset() = 0;
   virtual unsigned long Size() = 0;
   virtual unsigned long Flags() = 0;

   // CNC:2003-02-16 - If this is false, the Size of the pkgIndexFile must
   // 		       provide the number of elements, since a sequential
   // 		       counter will be used to verify progress.
   virtual bool OrderedOffset() {return true;}

   // CNC:2003-02-20 - This method will help on package ordering tasks,
   // 		       ensuring that if a package with the same version
   // 		       is installed, it won't be unexpectedly downloaded,
   // 		       even if with a "better" architecture or different
   // 		       dependencies.
   virtual bool IsDatabase() {return false;}

   virtual bool Step() = 0;

   inline bool HasFileDeps() {return FoundFileDeps;}
   virtual bool CollectFileProvides(pkgCache &Cache,
				    pkgCache::VerIterator &Ver) {return true;}

   ListParser() : FoundFileDeps(false) {}
   virtual ~ListParser() {}
};

std::unique_ptr<MMap> pkgMakeStatusCache(pkgSourceList &List,OpProgress &Progress,
                                         bool AllowMem = false);
std::unique_ptr<DynamicMMap> pkgMakeOnlyStatusCache(OpProgress &Progress);

#ifdef APT_COMPATIBILITY
#if APT_COMPATIBILITY != 986
#warning "Using APT_COMPATIBILITY"
#endif
std::unique_ptr<MMap> pkgMakeStatusCacheMem(pkgSourceList &List,OpProgress &Progress)
{
   return pkgMakeStatusCache(List,Progress,true);
}
#endif

#endif
