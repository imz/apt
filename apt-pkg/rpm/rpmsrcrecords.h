// Description								/*{{{*/
// $Id: rpmsrcrecords.h,v 1.5 2002/08/08 20:07:33 niemeyer Exp $
/* ######################################################################

   SRPM Records - Parser implementation for RPM style source indexes

   #####################################################################
 */
									/*}}}*/
#ifndef PKGLIB_RPMSRCRECORDS_H
#define PKGLIB_RPMSRCRECORDS_H

#include <apt-pkg/srcrecords.h>
#include <apt-pkg/fileutl.h>

#include "rpmhandler.h"

class rpmSrcRecordParser : public pkgSrcRecords::Parser
{
   RPMHandler *Handler;

   char *Buffer;
   unsigned int BufSize;
   unsigned int BufUsed;

   void BufCat(const char *text);
   void BufCat(const char *begin, const char *end);
   void BufCatTag(const char *tag, const char *value);
   void BufCatDep(Dependency *Dep);
   void BufCatDepList(unsigned int Type, unsigned int SubType,
		      const char *prefix);
   void BufCatDescr(const char *descr);

public:
   virtual bool Restart() override;
   virtual bool Step() override;
   virtual bool Jump(off_t Off) override;

   virtual string FileName() const override;

   virtual string Package() const override;
   virtual string Version() const override;
   virtual string Maintainer() const override;
   virtual string Section() const override;
   virtual string Changelog() const override;
   virtual const char **Binaries() override;
   virtual off_t Offset() override;
   virtual string AsStr() override;
   virtual bool Files(vector<pkgSrcRecords::File> &F) override;
   virtual bool BuildDepends(vector<BuildDepRec> &BuildDeps, bool ArchOnly) override;

   rpmSrcRecordParser(string File,pkgIndexFile const *Index);
   ~rpmSrcRecordParser();
};

#endif
