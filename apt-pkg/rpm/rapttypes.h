#ifndef PKGLIB_RAPTTYPES_H
#define PKGLIB_RAPTTYPES_H

/*
 * Layer of insulation against differing types used in rpm versions.
 * C happily converts enum to int etc automatically, C++ doesn't...
 */

#include <rpm/rpmtypes.h>
#include <rpm/rpmds.h>
typedef rpm_tag_t raptTag;
typedef rpm_tagtype_t raptTagType;
typedef rpmsenseFlags raptDepFlags;
typedef uint32_t raptInt;
typedef uint32_t raptDbOffset;
#define raptInitIterator(a,b,c,d) rpmtsInitIterator(a,(rpmTag)b,c,d)

#endif /* PKGLIB_RAPTTYPES_H */
