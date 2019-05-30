// Description								/*{{{*/
// $Id: rpmsrcrecords.h,v 1.5 2002/08/08 20:07:33 niemeyer Exp $
/* ######################################################################
   
   SRPM Records - Parser implementation for RPM style source indexes
   
   ##################################################################### 
 */
									/*}}}*/
#ifndef PKGLIB_RPMSRCRECORDS_H
#define PKGLIB_RPMSRCRECORDS_H

#ifdef __GNUG__
#pragma interface "apt-pkg/rpmsrcrecords.h"
#endif 

#include <apt-pkg/srcrecords.h>
#include <apt-pkg/fileutl.h>
#include <rpm/rpmlib.h>

  
class RPMHandler;

class rpmSrcRecordParser : public pkgSrcRecords::Parser
{
   RPMHandler *Handler;
   Header HeaderP;

   const char *StaticBinList[400];

   char *Buffer;
   unsigned int BufSize;
   unsigned int BufUsed;
   
   void BufCat(const char *text);
   void BufCat(const char *begin, const char *end);
   void BufCatTag(const char *tag, const char *value);
   void BufCatDep(const char *pkg, const char *version, int flags);
   void BufCatDescr(const char *descr);

public:
   virtual bool Restart();
   virtual bool Step(); 
   virtual bool Jump(unsigned long Off);

   virtual string FileName() const;

   virtual string Package() const;
   virtual string Version() const;
   virtual string Maintainer() const;
   virtual string Section() const;
   virtual string Changelog() const;
   virtual const char **Binaries();
   virtual unsigned long Offset();
   virtual string AsStr();
   virtual bool Files(vector<pkgSrcRecords::File> &F);
   virtual bool BuildDepends(vector<BuildDepRec> &BuildDeps, bool ArchOnly);

   rpmSrcRecordParser(string File,pkgIndexFile const *Index);
   ~rpmSrcRecordParser();
};

#endif
