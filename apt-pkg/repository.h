// CNC:2002-07-03

#ifndef PKGLIB_REPOSITORY_H
#define PKBLIB_REPOSITORY_H

#include <string>
#include <map>

#include <apt-pkg/sourcelist.h>

using std::map;

class pkgRepository
{
   protected:


   struct Checksum {
      unsigned long long Size;
      string Hash;
   };

   map<string,Checksum> IndexChecksums; // path -> checksum data

   bool GotRelease;
   string CheckMethod;

   public:

   string URI;
   string Dist;
   vector<string> FingerPrintList;
   string RootURI;

   bool Acquire;

   // LORG:2006-02-21 make these methods virtual
   virtual bool ParseRelease(string File);
   virtual bool HasRelease() const { return GotRelease; }

   virtual bool IsAuthenticated() const { return !FingerPrintList.empty(); }
   virtual bool FindChecksums(string URI,decltype(Checksum::Size) &Size, string &MD5) const;
   // LORG:2006-02-23
   virtual string GetCheckMethod() const {return CheckMethod;}
   virtual string GetComprMethod(const string URI) const {return "bz2";}

   pkgRepository(const string URI,const string Dist, const pkgSourceList::Vendor * const Vendor,
		 const string RootURI)
      : GotRelease(0), URI(URI), Dist(Dist), RootURI(RootURI),
	Acquire(1)
   {
      if (Vendor) FingerPrintList = Vendor->FingerPrintList;
   }

};

#endif

// vim:sts=3:sw=3
