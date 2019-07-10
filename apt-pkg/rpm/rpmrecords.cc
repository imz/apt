// Description								/*{{{*/
/* ######################################################################

   RPM Package Records - Parser for RPM package records

   #####################################################################
 */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#ifdef HAVE_RPM
#include <stdint.h>

#include <assert.h>

#include "rpmrecords.h"
#include "rpmhandler.h"
#include "rpmsystem.h"

#include <apt-pkg/error.h>
#include <apti18n.h>

#include <cstring>

// RecordParser::rpmRecordParser - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
rpmRecordParser::rpmRecordParser(const string &File, pkgCache &Cache)
   : Handler(0), Buffer(0), BufSize(0), BufUsed(0)
{
   if (File == RPMDBHandler::DataPath(false)) {
      IsDatabase = true;
      Handler = rpmSys.GetDBHandler();
   } else {
      IsDatabase = false;
      struct stat Buf;
      if (stat(File.c_str(),&Buf) == 0 && S_ISDIR(Buf.st_mode))
	 Handler = new RPMDirHandler(File);
      else if (flExtension(File) == "rpm")
	 Handler = new RPMSingleFileHandler(File);
      else
	 Handler = new RPMFileHandler(File);
   }
}
									/*}}}*/
// RecordParser::~rpmRecordParser - Destructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
rpmRecordParser::~rpmRecordParser()
{
   // Can't use Handler->IsDatabase here, since the RPMDBHandler
   // could already have been destroyed.
   if (IsDatabase == false)
      delete Handler;
   free(Buffer);
}
									/*}}}*/
// RecordParser::Jump - Jump to a specific record			/*{{{*/
// ---------------------------------------------------------------------
/* */
bool rpmRecordParser::Jump(pkgCache::VerFileIterator const &Ver)
{
   return Handler->Jump(Ver->Offset);
}
									/*}}}*/
// RecordParser::FileName - Return the archive filename on the site	/*{{{*/
// ---------------------------------------------------------------------
/* */
string rpmRecordParser::FileName()
{
   string Dir = Handler->Directory();
   if (Dir.empty() == true)
      return Handler->FileName();
   return flCombine(Dir, Handler->FileName());
}
									/*}}}*/
// RecordParser::Name - Return the package name				/*{{{*/
// ---------------------------------------------------------------------
/* */
string rpmRecordParser::Name()
{
   return Handler->Name();
}
									/*}}}*/
// RecordParser::MD5Hash - Return the archive hash			/*{{{*/
// ---------------------------------------------------------------------
/* */
string rpmRecordParser::MD5Hash()
{
   return Handler->MD5Sum();
}
									/*}}}*/
// RecordParser::Maintainer - Return the maintainer email		/*{{{*/
// ---------------------------------------------------------------------
/* */
string rpmRecordParser::Maintainer()
{
   return Handler->Maintainer();
}
									/*}}}*/
// RecordParser::ShortDesc - Return a 1 line description		/*{{{*/
// ---------------------------------------------------------------------
/* */
string rpmRecordParser::ShortDesc()
{
   return Handler->Summary();
}
									/*}}}*/
// RecordParser::LongDesc - Return a longer description			/*{{{*/
// ---------------------------------------------------------------------
/* */
string rpmRecordParser::LongDesc()
{
   return Handler->Description();
}
									/*}}}*/
// RecordParser::Changelog - Return package changelog if any		/*{{{*/
// -----------------------------------------------
string rpmRecordParser::Changelog()
{
   return Handler->Changelog();
}

									/*}}}*/
// RecordParser::SourcePkg - Return the source package name if any	/*{{{*/
// ---------------------------------------------------------------------
/* */
string rpmRecordParser::SourcePkg()
{
   return Handler->SourceRpm();
}
									/*}}}*/

void rpmRecordParser::BufCat(const char *text)
{
   if (text != NULL)
      BufCat(text, text+strlen(text));
}

void rpmRecordParser::BufCat(const char *begin, const char *end)
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

void rpmRecordParser::BufCatTag(const char *tag, const char *value)
{
   BufCat(tag);
   BufCat(value);
}

void rpmRecordParser::BufCatDep(Dependency *Dep)
{
   BufCat(Dep->Name.c_str());
   if (!Dep->Version.empty()) {
      BufCat(" (");
      switch (Dep->Op) {
         case pkgCache::Dep::Less:
            BufCat("<");
            break;
         case pkgCache::Dep::LessEq:
            BufCat("<=");
            break;
         case pkgCache::Dep::Equals:
            BufCat("=");
            break;
         case pkgCache::Dep::Greater:
            BufCat(">");
            break;
         case pkgCache::Dep::GreaterEq:
            BufCat(">=");
            break;
      }
      BufCat(" ");
      BufCat(Dep->Version.c_str());
      BufCat(")");
   }
}

void rpmRecordParser::BufCatDescr(const char *descr)
{
   const char *begin = descr;

   while (*descr) {
      if (*(descr++) == '\n') {
	 BufCat(" ");
	 BufCat(begin, descr);
	 begin = descr;
      }
   }
   if (*begin) {
      BufCat(" ");
      BufCat(begin, descr);
      BufCat("\n");
   }
}

void rpmRecordParser::BufCatDepList(unsigned int Type, unsigned int SubType,
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

// RecordParser::GetRec - The record in raw text, in std Debian format	/*{{{*/
// ---------------------------------------------------------------------
void rpmRecordParser::GetRec(const char *&Start,const char *&Stop)
{
   char buf[32];

   BufUsed = 0;

   BufCatTag("Package: ", Name().c_str());

   BufCatTag("\nSection: ", Handler->Group().c_str());

   snprintf(buf, sizeof(buf), "%llu", (unsigned long long) Handler->InstalledSize());
   BufCatTag("\nInstalled Size: ", buf);

   BufCatTag("\nMaintainer: ", Handler->Maintainer().c_str());

   BufCatTag("\nVersion: ", Handler->EVRDB().c_str());

   struct {
      unsigned int Type, SubType;
      const char *prefix;
   } dep_types[] = {
      { pkgCache::Dep::Depends, pkgCache::Dep::PreDepends, "\nPre-Depends: " },
      { pkgCache::Dep::Depends, pkgCache::Dep::Depends, "\nDepends: " },
      { pkgCache::Dep::Conflicts, pkgCache::Dep::Conflicts, "\nConflicts: " },
      { pkgCache::Dep::Provides, pkgCache::Dep::Provides, "\nProvides: " },
      { pkgCache::Dep::Obsoletes, pkgCache::Dep::Obsoletes, "\nObsoletes: " }
   };

   for (size_t i = 0; i < sizeof(dep_types) / sizeof(*dep_types); ++i)
      BufCatDepList(dep_types[i].Type, dep_types[i].SubType, dep_types[i].prefix);

   BufCatTag("\nArchitecture: ", Handler->Arch().c_str());

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

   Start = Buffer;
   Stop = Buffer + BufUsed;
}
									/*}}}*/

bool rpmRecordParser::HasFile(const char *File)
{
   return Handler->HasFile(File);
}

#endif /* HAVE_RPM */

// vim:sts=3:sw=3
