#include <config.h>
#include <iostream>

#include "raptheader.h"

using namespace std;

/*
 * Beginnings of C++ wrapper for RPM header and data.
 * Having a rpm version independent ways to access header data makes
 * things a whole lot saner in other places while permitting newer
 * rpm interfaces to be used where supported.
 *
 * Lots to do still:
 * - actually implement "raw" string mode + handle i18n strings correctly
 * - we'll want to eventually replace rpmhandler's HeaderP with raptHeader(),
 *   add constructors for reading in from files, db iterators & all etc...
 */

raptHeader::raptHeader(Header h)
{
   Hdr = headerLink(h);
}

raptHeader::~raptHeader()
{
   headerFree(Hdr);
}

bool raptHeader::hasTag(raptTag tag) const
{
   return headerIsEntry(Hdr, tag);
}

// XXX: should have a way to pass back formatting error messages
string raptHeader::format(const string &fmt) const
{
   string res;
   char *s = headerFormat(Hdr, fmt.c_str(), NULL);
   if (s) {
      res = string(s);
      free(s);
   }
   return res;
}

bool raptHeader::getTag(raptTag tag, raptInt &data) const
{
   vector<raptInt> _data;
   bool ret = false;

   if (getTag(tag, _data) && _data.size() == 1) {
      data = _data[0];
      ret = true;
   }
   return ret;
}

bool raptHeader::getTag(raptTag tag, string &data, bool raw) const
{
   vector<string> _data;
   bool ret = false;

   if (getTag(tag, _data, raw) && _data.size() == 1) {
      data = _data[0];
      ret = true;
   }
   return ret;
}

// use MINMEM to avoid extra copy from header if possible
#define HGDFL (headerGetFlags)(HEADERGET_EXT | HEADERGET_MINMEM)
#define HGRAW (headerGetFlags)(HGDFL | HEADERGET_RAW)

bool raptHeader::getTag(raptTag tag, vector<string> &data, bool raw) const
{
   struct rpmtd_s td;
   bool ret = false;
   headerGetFlags flags = raw ? HGRAW : HGDFL;

   if (headerGet(Hdr, tag, &td, flags)) {
      if (rpmtdClass(&td) == RPM_STRING_CLASS) {
	 const char *str;
	 while ((str = rpmtdNextString(&td))) {
	    data.push_back(str);
	 }
	 ret = true;
      }
      rpmtdFreeData(&td);
   }
   return ret;
}

bool raptHeader::getTag(raptTag tag, vector<raptInt> &data) const
{
   struct rpmtd_s td;
   bool ret = false;
   if (headerGet(Hdr, tag, &td, HGDFL)) {
      if (rpmtdType(&td) == RPM_INT32_TYPE) {
	 raptInt *num;
	 while ((num = rpmtdNextUint32(&td))) {
	    data.push_back(*num);
	 }
	 ret = true;
      }
      rpmtdFreeData(&td);
   }
   return ret;
}

// vim:sts=3:sw=3
