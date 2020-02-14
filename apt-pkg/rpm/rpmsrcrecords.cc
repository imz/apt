// Description								/*{{{*/
// $Id: rpmsrcrecords.cc,v 1.9 2003/01/29 15:19:02 niemeyer Exp $
/* ######################################################################

   SRPM Records - Parser implementation for RPM style source indexes

   #####################################################################
 */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#ifdef HAVE_RPM
#include <stdint.h>

#include <assert.h>

#include "rpmsrcrecords.h"
#include "rpmhandler.h"

#include <apt-pkg/error.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/pkgcache.h>

#include <apti18n.h>

#include <rpm/rpmds.h>

// SrcRecordParser::rpmSrcRecordParser - Constructor			/*{{{*/
// ---------------------------------------------------------------------
/* */
rpmSrcRecordParser::rpmSrcRecordParser(string File,pkgIndexFile const *Index)
    : Parser(Index), Buffer(0), BufSize(0), BufUsed(0)
{
   struct stat Buf;
   if (stat(File.c_str(),&Buf) == 0 && S_ISDIR(Buf.st_mode))
      Handler = new RPMDirHandler(File);
   else if (flExtension(File) == "rpm")
      Handler = new RPMSingleFileHandler(File);
   else
      Handler = new RPMFileHandler(File);
}
									/*}}}*/
// SrcRecordParser::~rpmSrcRecordParser - Destructor			/*{{{*/
// ---------------------------------------------------------------------
/* */
rpmSrcRecordParser::~rpmSrcRecordParser()
{
   delete Handler;
   free(Buffer);
}
									/*}}}*/
// SrcRecordParser::Binaries - Return the binaries field		/*{{{*/
// ---------------------------------------------------------------------
/* This member parses the binaries field into a pair of class arrays and
   returns a list of strings representing all of the components of the
   binaries field. The returned array need not be freed and will be
   reused by the next Binaries function call. */
const char **rpmSrcRecordParser::Binaries()
{
   return NULL;
}
									/*}}}*/
// SrcRecordParser::Files - Return a list of files for this source	/*{{{*/
// ---------------------------------------------------------------------
/* This parses the list of files and returns it, each file is required to have
   a complete source package */
bool rpmSrcRecordParser::Files(vector<pkgSrcRecords::File> &List)
{
   List.clear();

   pkgSrcRecords::File F;

   F.MD5Hash = Handler->MD5Sum();
   F.Size = Handler->FileSize();
   F.Path = flCombine(Handler->Directory(), Handler->FileName());
   F.Type = "srpm";

   List.push_back(F);

   return true;
}
									/*}}}*/

bool rpmSrcRecordParser::Restart()
{
   Handler->Rewind();
   return true;
}

bool rpmSrcRecordParser::Step()
{
   return Handler->Skip();
}

bool rpmSrcRecordParser::Jump(off_t Off)
{
   return Handler->Jump(Off);
}

// RecordParser::FileName - Return the archive filename on the site	/*{{{*/
// ---------------------------------------------------------------------
/* */
string rpmSrcRecordParser::FileName() const
{
   return Handler->FileName();
}
									/*}}}*/

string rpmSrcRecordParser::Package() const
{
   return Handler->Name();
}

string rpmSrcRecordParser::Version() const
{
   return Handler->EVRDB();
}


// RecordParser::Maintainer - Return the maintainer email		/*{{{*/
// ---------------------------------------------------------------------
/* */
string rpmSrcRecordParser::Maintainer() const
{
   return Handler->Maintainer();
}

string rpmSrcRecordParser::Section() const
{
   return Handler->Group();
}

// SrcRecordParser::Changelog - Package changelog
// ----------------------------------------------
string rpmSrcRecordParser::Changelog() const
{
   return Handler->Changelog();
}

off_t rpmSrcRecordParser::Offset()
{
    return Handler->Offset();
}

void rpmSrcRecordParser::BufCat(const char *text)
{
   if (text != NULL)
      BufCat(text, text+strlen(text));
}

void rpmSrcRecordParser::BufCat(const char *begin, const char *end)
{
   unsigned len = end - begin;

   while (BufUsed + len + 1 >= BufSize)
   {
      size_t new_size = BufSize + 512;
      char *new_buf = (char*)realloc(Buffer, new_size);
      if (new_buf == NULL)
      {
	 _error->Errno("realloc", _("Could not allocate buffer for record text"));
	 return;
      }
      Buffer = new_buf;
      BufSize = new_size;
   }

   memcpy(Buffer+BufUsed, begin, len);
   BufUsed += len;
   Buffer[BufUsed] = '\0';
}

void rpmSrcRecordParser::BufCatTag(const char *tag, const char *value)
{
   BufCat(tag);
   BufCat(value);
}

void rpmSrcRecordParser::BufCatDep(Dependency *Dep)
{
   string buf;

   BufCat(Dep->Name.c_str());
   if (Dep->Version.empty() == false)
   {
      BufCat(" (");
      switch (Dep->Op) {
         case pkgCache::Dep::Less:
            buf += "<";
            break;
         case pkgCache::Dep::LessEq:
            buf += "<=";
            break;
         case pkgCache::Dep::Equals:
            buf += "=";
            break;
         case pkgCache::Dep::Greater:
            buf += ">";
            break;
         case pkgCache::Dep::GreaterEq:
            buf += ">=";
            break;
      }

      BufCat(buf.c_str());
      BufCat(" ");
      BufCat(Dep->Version.c_str());
      BufCat(")");
   }
}

void rpmSrcRecordParser::BufCatDescr(const char *descr)
{
   const char *begin = descr;
   const char *p = descr;

   while (*p)
   {
      if (*p=='\n')
      {
	 BufCat(" ");
	 BufCat(begin, p+1);
	 begin = p+1;
      }
      p++;
   }
   if (*begin) {
      BufCat(" ");
      BufCat(begin, p);
      BufCat("\n");
   }
}

void rpmSrcRecordParser::BufCatDepList(unsigned int Type, unsigned int SubType,
				       const char *prefix)
{
   vector<Dependency*> Deps;
   if (!Handler->DepsList(Type, Deps, false))
      return;

   bool start = true;
   for (vector<Dependency*>::const_iterator I = Deps.begin(); I != Deps.end(); ++I) {
      if ((*I)->Type != SubType)
	 continue;
      if (start) {
	 BufCat(prefix);
	 start = false;
      } else {
	 BufCat(", ");
      }
      BufCatDep(*I);
      delete (*I);
   }
}

// SrcRecordParser::AsStr - The record in raw text
// -----------------------------------------------
string rpmSrcRecordParser::AsStr()
{
   char buf[32];

   BufUsed = 0;

   BufCatTag("Package: ", Handler->Name().c_str());

   BufCatTag("\nSection: ", Handler->Group().c_str());

   snprintf(buf, sizeof(buf), "%llu", (unsigned long long) Handler->InstalledSize());
   BufCatTag("\nInstalled Size: ", buf);

   BufCatTag("\nMaintainer: ", Handler->Maintainer().c_str());

   BufCatTag("\nVersion: ", Handler->EVRDB().c_str());

   BufCatDepList(pkgCache::Dep::Depends, pkgCache::Dep::Depends,
		 "\nBuild-Depends: ");
   // Doesn't do anything yet, build conflicts aren't recorded yet...
   BufCatDepList(pkgCache::Dep::Conflicts, pkgCache::Dep::Conflicts,
		 "\nBuild-Conflicts: ");

   snprintf(buf, sizeof(buf), "%llu", (unsigned long long) Handler->FileSize());
   BufCatTag("\nSize: ", buf);

   BufCatTag("\nMD5Sum: ", Handler->MD5Sum().c_str());

   BufCatTag("\nFilename: ", Handler->FileName().c_str());

   BufCatTag("\nDescription: ", Handler->Summary().c_str());
   BufCat("\n");
   BufCatDescr(Handler->Description().c_str());

   string changelog = Handler->Changelog();
   if (!changelog.empty()) {
      BufCat("Changelog:\n");
      BufCatDescr(changelog.c_str());
   }

   BufCat("\n");

   return string(Buffer, BufUsed);
}


// SrcRecordParser::BuildDepends - Return the Build-Depends information	/*{{{*/
// ---------------------------------------------------------------------
bool rpmSrcRecordParser::BuildDepends(vector<pkgSrcRecords::Parser::BuildDepRec> &BuildDeps,
				      bool ArchOnly)
{
   BuildDeps.clear();
   vector<Dependency*>::const_iterator I;

   vector<Dependency*> Deps;
   Handler->DepsList(pkgCache::Dep::Depends, Deps, false);

   for (I = Deps.begin(); I != Deps.end(); ++I) {
      BuildDepRec rec;
      rec.Package = (*I)->Name;
      rec.Version = (*I)->Version;
      rec.Op = (*I)->Op;
      rec.Type = pkgSrcRecords::Parser::BuildDepend;
      BuildDeps.push_back(rec);
      delete (*I);
   }

   vector<Dependency*> Conflicts;
   Handler->DepsList(pkgCache::Dep::Conflicts, Conflicts, false);

   for (I = Conflicts.begin(); I != Conflicts.end(); ++I) {
      BuildDepRec rec;
      rec.Package = (*I)->Name;
      rec.Version = (*I)->Version;
      rec.Op = (*I)->Op;
      rec.Type = pkgSrcRecords::Parser::BuildConflict;
      BuildDeps.push_back(rec);
      delete (*I);
   }
   return true;
}
									/*}}}*/
#endif /* HAVE_RPM */

// vim:sts=3:sw=3
