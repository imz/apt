// Description								/*{{{*/
// $Id: acqprogress.h,v 1.5 2003/02/02 22:24:11 jgg Exp $
/* ######################################################################

   Acquire Progress - Command line progress meter

   ##################################################################### */
									/*}}}*/
#ifndef ACQPROGRESS_H
#define ACQPROGRESS_H

#include <apt-pkg/acquire.h>

class AcqTextStatus : public pkgAcquireStatus
{
   unsigned int &ScreenWidth;
   char BlankLine[1024];
   unsigned long ID;
   unsigned long Quiet;

   public:

   virtual bool MediaChange(string Media,string Drive) override;
   virtual bool Authenticate(string Desc,string &User,string &Pass) override;
   virtual void IMSHit(pkgAcquire::ItemDesc &Itm) override;
   virtual void Fetch(pkgAcquire::ItemDesc &Itm) override;
   virtual void Done(pkgAcquire::ItemDesc &Itm) override;
   virtual void Fail(pkgAcquire::ItemDesc &Itm) override;
   virtual void Start() override;
   virtual void Stop() override;

   bool Pulse(pkgAcquire *Owner) override;

   AcqTextStatus(unsigned int &ScreenWidth,unsigned int Quiet);
};

#endif
