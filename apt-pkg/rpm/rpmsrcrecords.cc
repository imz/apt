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

#define ALT_RPM_API
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
    : Parser(Index), HeaderP(0), Buffer(0), BufSize(0), BufUsed(0)
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
   assert(HeaderP != NULL);

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
   if (Handler->Skip() == false)
       return false;
   HeaderP = Handler->GetHeader();
   return true;
}

bool rpmSrcRecordParser::Jump(off_t Off)
{
   if (!Handler->Jump(Off))
       return false;
   HeaderP = Handler->GetHeader();
   return true;
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
   char *str;
   string rval("");

   str = headerSprintf(HeaderP,
         "[* %{CHANGELOGTIME:day} %{CHANGELOGNAME}\n%{CHANGELOGTEXT}\n\n]",
         rpmTagTable, rpmHeaderFormats, NULL);

   if (str && *str) {
	  rval = (const char *)str;
   }
   if (str)
      free(str);

   return rval;
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

void rpmSrcRecordParser::BufCatDep(const char *pkg,
				   const char *version,
				   int flags)
{
   char buf[16];
   char *ptr = (char*)buf;

   BufCat(pkg);
   if (*version)
   {
      *ptr++ = ' ';
      *ptr++ = '(';
      if (flags & RPMSENSE_LESS)
	 *ptr++ = '<';
      if (flags & RPMSENSE_GREATER)
	 *ptr++ = '>';
      if (flags & RPMSENSE_EQUAL)
	 *ptr++ = '=';
      *ptr++ = ' ';
      *ptr = '\0';

      BufCat(buf);
      BufCat(version);
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

// SrcRecordParser::AsStr - The record in raw text
// -----------------------------------------------
string rpmSrcRecordParser::AsStr()
{
   // FIXME: This method is leaking memory from headerGetEntry().
   rpm_tagtype_t type, type2, type3;
   rpm_count_t count;
   char *str;
   char **strv;
   char **strv2;
   int32_t *numv;
   char buf[32];

   BufUsed = 0;

   BufCatTag("Package: ", Handler->Name().c_str());

   BufCatTag("\nSection: ", Handler->Group().c_str());

   snprintf(buf, sizeof(buf), "%llu", (unsigned long long) Handler->InstalledSize());
   BufCatTag("\nInstalled Size: ", buf);

   BufCatTag("\nMaintainer: ", Handler->Maintainer().c_str());

   BufCatTag("\nVersion: ", Handler->EVRDB().c_str());

   headerGetEntry(HeaderP, RPMTAG_REQUIRENAME, &type, (void **)&strv, &count);
   assert(type == RPM_STRING_ARRAY_TYPE || count == 0);

   headerGetEntry(HeaderP, RPMTAG_REQUIREVERSION, &type2, (void **)&strv2, &count);
   headerGetEntry(HeaderP, RPMTAG_REQUIREFLAGS, &type3, (void **)&numv, &count);

   if (count > 0)
   {
      int i, j;

      for (j = i = 0; i < count; i++)
      {
	 if ((numv[i] & RPMSENSE_PREREQ))
	 {
	    if (j == 0)
		BufCat("\nPre-Depends: ");
	    else
		BufCat(", ");
	    BufCatDep(strv[i], strv2[i], numv[i]);
	    j++;
	 }
      }

      for (j = 0, i = 0; i < count; i++)
      {
	 if (!(numv[i] & RPMSENSE_PREREQ))
	 {
	    if (j == 0)
		BufCat("\nDepends: ");
	    else
		BufCat(", ");
	    BufCatDep(strv[i], strv2[i], numv[i]);
	    j++;
	 }
      }
   }

   headerGetEntry(HeaderP, RPMTAG_CONFLICTNAME, &type, (void **)&strv, &count);
   assert(type == RPM_STRING_ARRAY_TYPE || count == 0);

   headerGetEntry(HeaderP, RPMTAG_CONFLICTVERSION, &type2, (void **)&strv2, &count);
   headerGetEntry(HeaderP, RPMTAG_CONFLICTFLAGS, &type3, (void **)&numv, &count);

   if (count > 0)
   {
      BufCat("\nConflicts: ");
      for (int i = 0; i < count; i++)
      {
	 if (i > 0)
	     BufCat(", ");
	 BufCatDep(strv[i], strv2[i], numv[i]);
      }
   }

   snprintf(buf, sizeof(buf), "%llu", (unsigned long long) Handler->FileSize());
   BufCatTag("\nSize: ", buf);

   BufCatTag("\nMD5Sum: ", Handler->MD5Sum().c_str());

   BufCatTag("\nFilename: ", Handler->FileName().c_str());

   BufCatTag("\nDescription: ", Handler->Summary().c_str());
   BufCat("\n");
   BufCatDescr(Handler->Description().c_str());

   str = headerSprintf(HeaderP,
         "[* %{CHANGELOGTIME:day} %{CHANGELOGNAME}\n%{CHANGELOGTEXT}\n]",
         rpmTagTable, rpmHeaderFormats, NULL);
   if (str && *str) {
      BufCat("Changelog:\n");
      BufCatDescr(str);
   }
   if (str)
      free(str);

   BufCat("\n");

   return string(Buffer, BufUsed);
}


// SrcRecordParser::BuildDepends - Return the Build-Depends information	/*{{{*/
// ---------------------------------------------------------------------
bool rpmSrcRecordParser::BuildDepends(vector<pkgSrcRecords::Parser::BuildDepRec> &BuildDeps,
				      bool ArchOnly)
{
   // FIXME: This method is leaking memory from headerGetEntry().
   int RpmTypeTag[] = {RPMTAG_REQUIRENAME,
		       RPMTAG_REQUIREVERSION,
		       RPMTAG_REQUIREFLAGS,
		       RPMTAG_CONFLICTNAME,
		       RPMTAG_CONFLICTVERSION,
		       RPMTAG_CONFLICTFLAGS};
   int BuildType[] = {pkgSrcRecords::Parser::BuildDepend,
		      pkgSrcRecords::Parser::BuildConflict};
   BuildDepRec rec;

   BuildDeps.clear();

   for (unsigned char Type = 0; Type != 2; Type++)
   {
      char **namel = NULL;
      char **verl = NULL;
      int *flagl = NULL;
      int res;
      rpm_tagtype_t type;
      rpm_count_t count;

      res = headerGetEntry(HeaderP, RpmTypeTag[0+Type*3], &type,
			 (void **)&namel, &count);
      if (res != 1)
	 return true;
      res = headerGetEntry(HeaderP, RpmTypeTag[1+Type*3], &type,
			 (void **)&verl, &count);
      res = headerGetEntry(HeaderP, RpmTypeTag[2+Type*3], &type,
			 (void **)&flagl, &count);

      for (int i = 0; i < count; i++)
      {
	 if (strncmp(namel[i], "rpmlib", 6) == 0)
	 {
	    /* 4.13.0 (ALT specific) */
	    int res = rpmCheckRpmlibProvides(namel[i], verl?verl[i]:NULL,
					     flagl[i]);
	    if (res) continue;
	 }

	 if (verl)
	 {
	    if (!*verl[i])
	       rec.Op = pkgCache::Dep::NoOp;
	    else
	    {
	       if (flagl[i] & RPMSENSE_LESS)
	       {
		  if (flagl[i] & RPMSENSE_EQUAL)
		      rec.Op = pkgCache::Dep::LessEq;
		  else
		      rec.Op = pkgCache::Dep::Less;
	       }
	       else if (flagl[i] & RPMSENSE_GREATER)
	       {
		  if (flagl[i] & RPMSENSE_EQUAL)
		      rec.Op = pkgCache::Dep::GreaterEq;
		  else
		      rec.Op = pkgCache::Dep::Greater;
	       }
	       else if (flagl[i] & RPMSENSE_EQUAL)
		  rec.Op = pkgCache::Dep::Equals;
	    }

	    rec.Version = verl[i];
	 }
	 else
	 {
	    rec.Op = pkgCache::Dep::NoOp;
	    rec.Version = "";
	 }

	 rec.Type = BuildType[Type];
	 rec.Package = namel[i];
	 BuildDeps.push_back(rec);
      }
   }
   return true;
}
									/*}}}*/
#endif /* HAVE_RPM */

// vim:sts=3:sw=3
