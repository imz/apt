// Description								/*{{{*/
/* ######################################################################

   Update - ListUpdate related code

   ##################################################################### */
									/*}}}*/

#ifndef PKGLIB_UPDATE_H
#define PKGLIB_UPDATE_H

class pkgAcquireStatus;
class pkgSourceList;
class pkgCacheFile;

bool ListUpdate(pkgAcquireStatus &progress, pkgSourceList &List, pkgCacheFile &Cache);

#endif
