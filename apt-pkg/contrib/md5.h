// Description								/*{{{*/
// $Id: md5.h,v 1.1 2002/07/23 17:54:51 niemeyer Exp $
/* ######################################################################

   MD5SumValue - Storage for a MD5Sum
   MD5Summation - MD5 Message Digest Algorithm.

   This is a C++ interface to a set of MD5Sum functions. The class can
   store a MD5Sum in 16 bytes of memory.

   A MD5Sum is used to generate a (hopefully) unique 16 byte number for a
   block of data. This can be used to gaurd against corruption of a file.
   MD5 should not be used for tamper protection, use SHA or something more
   secure.

   There are two classes because computing a MD5 is not a continual
   operation unless 64 byte blocks are used. Also the summation requires an
   extra 18*4 bytes to operate.

   ##################################################################### */
									/*}}}*/
#ifndef APTPKG_MD5_H
#define APTPKG_MD5_H

#include <string>
#include <cstring>

using std::string;

class MD5Summation;

class MD5SumValue
{
   friend class MD5Summation;
   unsigned char Sum[4*4];

   public:

   // Accessors
   bool operator ==(const MD5SumValue &rhs) const;
   string Value() const;
   inline operator string() const {return Value();}
   bool Set(string Str);

   MD5SumValue(string Str);
   MD5SumValue();
};

class MD5Summation
{
   unsigned char Buf[4*4];
   unsigned char Bytes[2*4];
   unsigned char In[16*4];
   bool Done;

   public:

   bool Add(const unsigned char *Data,unsigned long Size);
   inline bool Add(const char *Data) {return Add((unsigned char *)Data,strlen(Data));}
   bool AddFD(int Fd,unsigned long Size);
   inline bool Add(const unsigned char *Beg,const unsigned char *End)
                  {return Add(Beg,End-Beg);}
   MD5SumValue Result();

   MD5Summation();
};

#endif
