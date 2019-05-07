// Description								/*{{{*/
// $Id: rpmversion.cc,v 1.4 2002/11/19 13:03:29 niemeyer Exp $
/* ######################################################################

   RPM Version - Versioning system for RPM

   This implements the standard RPM versioning system.
   
   ##################################################################### 
 */
									/*}}}*/
// Include Files							/*{{{*/
#ifdef __GNUG__
#pragma implementation "apt-pkg/rpmversion.h"
#endif

#include <config.h>

#ifdef HAVE_RPM

#include <apt-pkg/rpmversion.h>
#include <apt-pkg/pkgcache.h>

#define ALT_RPM_API
#include <rpm/rpmlib.h>

#include <stdlib.h>
#include <assert.h>

#if RPM_VERSION >= 0x040100
#include <rpm/rpmds.h>
#endif

rpmVersioningSystem rpmVS;

// rpmVS::rpmVersioningSystem - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
rpmVersioningSystem::rpmVersioningSystem()
{
   Label = "Standard .rpm";
}
									/*}}}*/

/* Having the const modifier on the values of the output parameters may seem
   unjustified, but this has the nice property that they cannot be freed;
   e.g., free(*vp) in subsequent code would be invalid.
   (And that's good because it's just a pointer into a larger allocated chunk.)
*/
static void parseEVRT(char * const evrt,
                      const char ** const ep,
                      const char ** const vp,
                      const char ** const rp,
                      const char ** const tp)
{
   const char *buildtime = NULL;
   {
      char *s = &evrt[strlen(evrt)];
      while (s != evrt) {
         --s;
         if (*s == '@') {
            buildtime = &s[1];
            *s = 0;
            break;
         }
         if (!isdigit(*s))
            break;
      }
   }
   if (tp) *tp = buildtime;
   parseEVR(evrt, ep, vp, rp);
}

// rpmVS::CmpVersion - Comparison for versions				/*{{{*/
// ---------------------------------------------------------------------
/* This fragments the version into E:V-R triples and compares each 
   portion separately. */
int rpmVersioningSystem::DoCmpVersion(const char *A,const char *AEnd,
				      const char *B,const char *BEnd)
{
   char * const tmpA = strndupa(A, (size_t)(AEnd-A));
   char * const tmpB = strndupa(B, (size_t)(BEnd-B));
   const char *AE, *AV, *AR, *AT;
   const char *BE, *BV, *BR, *BT;
   int rc = 0;
   parseEVRT(tmpA, &AE, &AV, &AR, &AT);
   parseEVRT(tmpB, &BE, &BV, &BR, &BT);
   if (AE && !BE)
       rc = 1;
   else if (!AE && BE)
       rc = -1;
   else if (AE && BE)
   {
      int AEi, BEi;
      AEi = atoi(AE);
      BEi = atoi(BE);
      if (AEi < BEi)
	  rc = -1;
      else if (AEi > BEi)
	  rc = 1;
   }
   if (rc == 0)
   {
      rc = rpmvercmp(AV, BV);
      if (rc == 0) {
	  if (AR && !BR)
	      rc = 1;
	  else if (!AR && BR)
	      rc = -1;
	  else if (AR && BR)
	      rc = rpmvercmp(AR, BR);
      }
   }
   return rc;
}
									/*}}}*/
// rpmVS::DoCmpVersionArch - Compare versions, using architecture	/*{{{*/
// ---------------------------------------------------------------------
/* */
int rpmVersioningSystem::DoCmpVersionArch(const char *A,const char *Aend,
					  const char *AA,const char *AAend,
					  const char *B,const char *Bend,
					  const char *BA,const char *BAend)
{
   int rc = DoCmpVersion(A, Aend, B, Bend);
   if (rc == 0)
   {
      int aa = rpmMachineScore(RPM_MACHTABLE_INSTARCH, AA); 
      int ba = rpmMachineScore(RPM_MACHTABLE_INSTARCH, BA); 
      if (aa < ba)
	 rc = 1;
      else if (aa > ba)
	 rc = -1;
   }
   return rc;
}
									/*}}}*/
// rpmVS::CheckDep - Check a single dependency				/*{{{*/
// ---------------------------------------------------------------------
/* This simply preforms the version comparison and switch based on 
   operator. If DepVer is 0 then we are comparing against a provides
   with no version. */
bool rpmVersioningSystem::CheckDep(const char *PkgVer,
				   int Op,const char *DepVer)
{
   int PkgFlags = RPMSENSE_EQUAL;
   int DepFlags = 0;
   bool invert = false;
   int rc;
   
   switch (Op & 0x0F)
   {
    case pkgCache::Dep::LessEq:
      DepFlags = RPMSENSE_LESS|RPMSENSE_EQUAL;
      break;

    case pkgCache::Dep::GreaterEq:
      DepFlags = RPMSENSE_GREATER|RPMSENSE_EQUAL;
      break;
      
    case pkgCache::Dep::Less:
      DepFlags = RPMSENSE_LESS;
      break;
      
    case pkgCache::Dep::Greater:
      DepFlags = RPMSENSE_GREATER;
      break;

    case pkgCache::Dep::Equals:
      DepFlags = RPMSENSE_EQUAL;
      break;
      
    case pkgCache::Dep::NotEquals:
      DepFlags = RPMSENSE_EQUAL;
      invert = true;
      break;
      
    default:
      // optimize: no need to check version
      return true;
      // old code:
      DepFlags = RPMSENSE_ANY;
      break;
   }

   // optimize: equal version strings => equal versions
   if ((DepFlags & RPMSENSE_SENSEMASK) == RPMSENSE_EQUAL)
      if (PkgVer && DepVer)
	 if (strcmp(PkgVer, DepVer) == 0)
	    return invert ? false : true;

#if HAVE_RPMRANGESOVERLAP && RPM_VERSION >= 0x040d00 /* 4.13.0 (ALT specific) */
   rc = rpmRangesOverlap("", PkgVer, PkgFlags, "", DepVer, DepFlags, _rpmds_nopromote);
#elif RPM_VERSION >= 0x040100
   rpmds pds = rpmdsSingle(RPMTAG_PROVIDENAME, "", PkgVer, PkgFlags);
   rpmds dds = rpmdsSingle(RPMTAG_REQUIRENAME, "", DepVer, DepFlags);
# if RPM_VERSION >= 0x040201
   rpmdsSetNoPromote(pds, _rpmds_nopromote);
   rpmdsSetNoPromote(dds, _rpmds_nopromote);
# endif
   rc = rpmdsCompare(pds, dds);
   rpmdsFree(pds);
   rpmdsFree(dds);
#else 
   rc = rpmRangesOverlap("", PkgVer, PkgFlags, "", DepVer, DepFlags);
#endif
    
   return (!invert && rc) || (invert && !rc);
}
									/*}}}*/
// rpmVS::CheckDep - Check a single dependency				/*{{{*/
// ---------------------------------------------------------------------
/* This prototype is a wrapper over CheckDep above. It's useful in the
   cases where the kind of dependency matters to decide if it matches
   or not */
bool rpmVersioningSystem::CheckDep(const char *PkgVer,
				   pkgCache::DepIterator Dep)
{
   if (Dep->Type == pkgCache::Dep::Obsoletes &&
       (PkgVer == 0 || PkgVer[0] == 0))
      return false;
   return CheckDep(PkgVer,Dep->CompareOp,Dep.TargetVer());
}
									/*}}}*/
// rpmVS::UpstreamVersion - Return the upstream version string		/*{{{*/
// ---------------------------------------------------------------------
/* This strips all the vendor specific information from the version number */
string rpmVersioningSystem::UpstreamVersion(const char *Ver)
{
   // Strip off the bit before the first colon
   const char *I = Ver;
   for (; *I != 0 && *I != ':'; I++);
   if (*I == ':')
      Ver = I + 1;
   
   // Chop off the trailing -
   I = Ver;
   unsigned Last = strlen(Ver);
   for (; *I != 0; I++)
      if (*I == '-')
	 Last = I - Ver;
   
   return string(Ver,Last);
}
									/*}}}*/

#endif /* HAVE_RPM */

// vim:sts=3:sw=3
