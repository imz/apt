// Description								/*{{{*/
// $Id: rpmversion.h,v 1.1 2002/07/23 17:54:51 niemeyer Exp $
/* ######################################################################

   RPM Version - Versioning system for RPM

   This implements the RPM versioning system.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_RPMVERSION_H
#define PKGLIB_RPMVERSION_H

#include <apt-pkg/version.h>
#include <apt-pkg/strutl.h>

class rpmVersioningSystem : public pkgVersioningSystem
{
   public:

   // Compare versions..
   virtual int DoCmpVersion(const char *A,const char *Aend,
			    const char *B,const char *Bend) override;
   virtual int DoCmpVersionArch(const char *A,const char *Aend,
				const char *AA,const char *AAend,
				const char *B,const char *Bend,
				const char *BA,const char *BAend) override;
   virtual bool CheckDep(const char *PkgVer,int Op,const char *DepVer) override;
   virtual bool CheckDep(const char *PkgVer,pkgCache::DepIterator Dep) override;
   virtual int DoCmpReleaseVer(const char *A,const char *Aend,
			     const char *B,const char *Bend) override
   {
      return DoCmpVersion(A,Aend,B,Bend);
   }
   virtual string UpstreamVersion(const char *A) override;

   rpmVersioningSystem();
};

extern rpmVersioningSystem rpmVS;

#endif
