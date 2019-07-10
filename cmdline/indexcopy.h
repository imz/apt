// Description								/*{{{*/
// $Id: indexcopy.h,v 1.1 2002/07/23 17:54:52 niemeyer Exp $
/* ######################################################################

   Index Copying - Aid for copying and verifying the index files

   ##################################################################### */
									/*}}}*/
#ifndef INDEXCOPY_H
#define INDEXCOPY_H

#include <vector>
#include <string>
#include <stdio.h>

using std::string;
using std::vector;

class pkgTagSection;
class FileFd;

class IndexCopy
{
   protected:

   pkgTagSection *Section;

   string ChopDirs(const string &Path,unsigned int Depth);
   bool ReconstructPrefix(string &Prefix,const string &OrigPath,const string &CD,
			  const string &File);
   bool ReconstructChop(unsigned long &Chop,const string &Dir,const string &File);
   void ConvertToSourceList(const string &CD,string &Path);
   bool GrabFirst(const string &Path,string &To,unsigned int Depth);
   virtual bool GetFile(string &Filename,unsigned long &Size) = 0;
   virtual bool RewriteEntry(FILE *Target,const string &File) = 0;
   virtual const char *GetFileName() = 0;
   virtual const char *Type() = 0;

   public:

   bool CopyPackages(const string &CDROM,const string &Name,vector<string> &List);
};

class PackageCopy : public IndexCopy
{
   protected:

   virtual bool GetFile(string &Filename,unsigned long &Size) override;
   virtual bool RewriteEntry(FILE *Target,const string &File) override;
   virtual const char *GetFileName() override {return "Packages";}
   virtual const char *Type() override {return "Package";}

   public:
};

class SourceCopy : public IndexCopy
{
   protected:

   virtual bool GetFile(string &Filename,unsigned long &Size) override;
   virtual bool RewriteEntry(FILE *Target,const string &File) override;
   virtual const char *GetFileName() override {return "Sources";}
   virtual const char *Type() override {return "Source";}

   public:
};

#endif
