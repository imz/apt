// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: debsrcrecords.h,v 1.1.1.1 2000/08/10 12:42:39 kojima Exp $
/* ######################################################################
   
   Debian Source Package Records - Parser implementation for Debian style
                                   source indexes
   
   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_DEBSRCRECORDS_H
#define PKGLIB_DEBSRCRECORDS_H

#ifdef __GNUG__
#pragma interface "apt-pkg/debsrcrecords.h"
#endif 

#include <apt-pkg/srcrecords.h>
#include <apt-pkg/tagfile.h>

class debSrcRecordParser : public pkgSrcRecords::Parser
{
   pkgTagFile Tags;
   pkgTagSection Sect;
   char Buffer[10000];
   const char *StaticBinList[400];
   unsigned long iOffset;
   
   public:

   virtual bool Restart() {return Tags.Jump(Sect,0);};
   virtual bool Step() {iOffset = Tags.Offset(); return Tags.Step(Sect);};
   virtual bool Jump(unsigned long Off) {iOffset = Off; return Tags.Jump(Sect,Off);};

   virtual string Package() {return Sect.FindS("Package");};
   virtual string Version() {return Sect.FindS("Version");};
   virtual string Maintainer() {return Sect.FindS("Maintainer");};
   virtual string Section() {return Sect.FindS("Section");};
   virtual const char **Binaries();
   virtual unsigned long Offset() {return iOffset;};
   virtual string AsStr() 
   {
      const char *Start=0,*Stop=0;
      Sect.GetSection(Start,Stop);
      return string(Start,Stop);
   };
   virtual bool Files(vector<pkgSrcRecords::File> &F);
   
   debSrcRecordParser(FileFd *File,pkgSourceList::const_iterator SrcItem) : 
                   Parser(File,SrcItem),
                   Tags(*File,sizeof(Buffer)) {};
};

#endif
