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

#include <rpm/rpmds.h>

rpmVersioningSystem rpmVS;

// rpmVS::rpmVersioningSystem - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
rpmVersioningSystem::rpmVersioningSystem()
{
   Label = "Standard .rpm";
}
									/*}}}*/
static std::ptrdiff_t index_of_EVR_postfix(const char * const evrt)
{
   const char *s = &evrt[strlen(evrt)];
   while (s != evrt) {
      --s;
      if (*s == '@') {
         return s - evrt;
      }
      if (!isdigit(*s))
         break;
   }
   return -1;
}

/* Having the const modifier on the values of the output parameters may seem
   unjustified, but this has the nice property that they cannot be freed;
   e.g., free(*vp) in subsequent code would be invalid.
   (And that's good because it's just a pointer into a larger allocated chunk.)
*/
static void parseEVRDT(char * const evrt,
                       const char ** const ep,
                       const char ** const vp,
                       const char ** const rp,
                       const char ** const dp,
                       const char ** const tp)
{
   char *buildtime = NULL;
   {
      const std::ptrdiff_t i = index_of_EVR_postfix(evrt);
      if (i >= 0) {
         evrt[i] = '\0';
         buildtime = &evrt[i+1];
      }
   }
   parseEVRD(evrt, ep, vp, rp, dp);
   if (tp) *tp = buildtime;
}

static void parseEVRDTstruct(char * const s,
                             struct rpmEVRDT * const res)
{
   const char *EpochStr, *BuildtimeStr;
   parseEVRDT(s,
              &EpochStr,
              &res->version, &res->release, &res->disttag,
              &BuildtimeStr);
   if (EpochStr) {
      res->has_epoch = true;
      res->epoch = strtoull(EpochStr, NULL, 10);
   }
   else {
      res->has_epoch = false;
   }
   if (BuildtimeStr) {
      res->has_buildtime = true;
      res->buildtime = strtoull(BuildtimeStr, NULL, 10);
   }
   else {
      res->has_buildtime = false;
   }
}

// rpmVS::CmpVersion - Comparison for versions				/*{{{*/
// ---------------------------------------------------------------------
/* This fragments the version into E:V-R triples and compares each
   portion separately. */
int rpmVersioningSystem::DoCmpVersion(const char *A,const char *AEnd,
				      const char *B,const char *BEnd)
{
   struct rpmEVRDT AVerInfo, BVerInfo;
   char * const tmpA = strndupa(A, (size_t)(AEnd-A));
   char * const tmpB = strndupa(B, (size_t)(BEnd-B));
   parseEVRDTstruct(tmpA, &AVerInfo);
   parseEVRDTstruct(tmpB, &BVerInfo);
   return rpmEVRDTCompare(&AVerInfo, &BVerInfo);
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

   if (PkgVer) {
      const std::ptrdiff_t PkgVer_len = index_of_EVR_postfix(PkgVer);

      // optimize: equal version strings => equal versions
      if (DepVer && (DepFlags & RPMSENSE_SENSEMASK) == RPMSENSE_EQUAL) {
         const bool rc = (PkgVer_len >= 0) ?
            strncmp(PkgVer, DepVer, (std::size_t)PkgVer_len) == 0
            && DepVer[PkgVer_len] == '\0'
            : strcmp(PkgVer, DepVer) == 0;
         if (rc)
            return invert ? false : true;
      }

      if (PkgVer_len >= 0)
         PkgVer = strndupa(PkgVer, (std::size_t)PkgVer_len);
   }

   /* 4.13.0 (ALT specific) */
   const int rc = rpmRangesOverlap("", PkgVer, PkgFlags, "", DepVer, DepFlags, _rpmds_nopromote);

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
