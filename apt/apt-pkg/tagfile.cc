// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: tagfile.cc,v 1.2 2001/01/11 02:03:27 kojima Exp $
/* ######################################################################

   Fast scanner for RFC-822 type header information
   
   This uses a rotating buffer to load the package information into.
   The scanner runs over it and isolates and indexes a single section.
   
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#ifdef __GNUG__
#pragma implementation "apt-pkg/tagfile.h"
#endif

#include <apt-pkg/tagfile.h>
#include <apt-pkg/error.h>
#include <apt-pkg/strutl.h>

#include <i18n.h>

#include <string>
#include <stdio.h>
									/*}}}*/

// TagFile::pkgTagFile - Constructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgTagFile::pkgTagFile(FileFd &Fd,unsigned long Size) : Fd(Fd), Size(Size)
{
   Buffer = new char[Size];
   Start = End = Buffer;
   Left = Fd.Size();
   iOffset = 0;
   Fill();
}
									/*}}}*/
// pkgTagFile::~pkgTagFile - Destructor					/*{{{*/
// ---------------------------------------------------------------------
/* */
pkgTagFile::~pkgTagFile()
{
   delete [] Buffer;
}
									/*}}}*/
// TagFile::Step - Advance to the next section				/*{{{*/
// ---------------------------------------------------------------------
/* If the Section Scanner fails we refill the buffer and try again. */
bool pkgTagFile::Step(pkgTagSection &Tag)
{
   if (Tag.Scan(Start,End - Start) == false)
   {
      if (Fill() == false)
	 return false;
      
      if (Tag.Scan(Start,End - Start) == false)
	 return _error->Error(_("Unable to parse package file %s (1)"),Fd.Name().c_str());
   }   
   Start += Tag.size();
   iOffset += Tag.size();
   
   return true;
}
									/*}}}*/
// TagFile::Fill - Top up the buffer					/*{{{*/
// ---------------------------------------------------------------------
/* This takes the bit at the end of the buffer and puts it at the start
   then fills the rest from the file */
bool pkgTagFile::Fill()
{
   unsigned long EndSize = End - Start;
   
   memmove(Buffer,Start,EndSize);
   Start = Buffer;
   End = Buffer + EndSize;
   
   if (Left == 0)
   {
      if (EndSize <= 3)
	 return false;
      if (Size - (End - Buffer) < 4)
	 return true;
      
      // Append a double new line if one does not exist
      unsigned int LineCount = 0;
      for (const char *E = End - 1; E - End < 6 && (*E == '\n' || *E == '\r'); E--)
	 if (*E == '\n')
	    LineCount++;
      for (; LineCount < 2; LineCount++)
	 *End++ = '\n';
      
      return true;
   }
   
   // See if only a bit of the file is left
   if (Left < Size - (End - Buffer))
   {
      if (Fd.Read(End,Left) == false)
	 return false;
      
      End += Left;
      Left = 0;
   }
   else
   {
      if (Fd.Read(End,Size - (End - Buffer)) == false)
	 return false;
      
      Left -= Size - (End - Buffer);
      End = Buffer + Size;
   }   
   return true;
}
									/*}}}*/
// TagFile::Jump - Jump to a pre-recorded location in the file		/*{{{*/
// ---------------------------------------------------------------------
/* This jumps to a pre-recorded file location and reads the record
   that is there */
bool pkgTagFile::Jump(pkgTagSection &Tag,unsigned long Offset)
{
   iOffset = Offset;
   Left = Fd.Size() - Offset;
   if (Fd.Seek(Offset) == false)
      return false;
   End = Start = Buffer;
   
   if (Fill() == false)
      return false;

   if (Tag.Scan(Start,End - Start) == true)
      return true;
   
   // This appends a double new line (for the real eof handling)
   if (Fill() == false)
      return false;
   
   if (Tag.Scan(Start,End - Start) == false)
   {
      cout << string(Start,End) << endl;
      return _error->Error(_("Unable to parse package file %s (2)"),Fd.Name().c_str());
   }
   
   return true;
}
									/*}}}*/
// TagSection::Scan - Scan for the end of the header information	/*{{{*/
// ---------------------------------------------------------------------
/* This looks for the first double new line in the data stream. It also
   indexes the tags in the section. This very simple hash function for the
   first 3 letters gives very good performance on the debian package files */
bool pkgTagSection::Scan(const char *Start,unsigned long MaxLength)
{
   const char *End = Start + MaxLength;
   Stop = Section = Start;
   memset(AlphaIndexes,0,sizeof(AlphaIndexes));

   if (Stop == 0)
      return false;
   
   TagCount = 0;
   while (TagCount < sizeof(Indexes)/sizeof(Indexes[0]) && Stop < End)
   {
      // Start a new index and add it to the hash
      if (isspace(Stop[0]) == 0)
      {
	 Indexes[TagCount++] = Stop - Section;
	 unsigned char A = tolower(Stop[0]) - 'a';
	 unsigned char B = tolower(Stop[1]) - 'a';
	 unsigned char C = tolower(Stop[3]) - 'a';
	 AlphaIndexes[((A + C/3)%26) + 26*((B + C/2)%26)] = TagCount;
      }

      Stop = (const char *)memchr(Stop,'\n',End - Stop);
      
      if (Stop == 0)
	 return false;
      
      for (; Stop[1] == '\r' && Stop+1 < End; Stop++);

      // Double newline marks the end of the record
      if (Stop+1 < End && Stop[1] == '\n')
      {
	 Indexes[TagCount] = Stop - Section;
	 for (; (Stop[0] == '\n' || Stop[0] == '\r') && Stop < End; Stop++);
	 return true;
      }
      
      Stop++;
   }

   return false;
}
									/*}}}*/
// TagSection::Find - Locate a tag					/*{{{*/
// ---------------------------------------------------------------------
/* This searches the section for a tag that matches the given string. */
bool pkgTagSection::Find(const char *Tag,const char *&Start,
		         const char *&End)
{
   unsigned int Length = strlen(Tag);
   unsigned char A = tolower(Tag[0]) - 'a';
   unsigned char B = tolower(Tag[1]) - 'a';
   unsigned char C = tolower(Tag[3]) - 'a';
   unsigned int I = AlphaIndexes[((A + C/3)%26) + 26*((B + C/2)%26)];
   if (I == 0)
      return false;
   I--;
   
   for (unsigned int Counter = 0; Counter != TagCount; Counter++, 
	I = (I+1)%TagCount)
   {
      const char *St;
      St = Section + Indexes[I];
      if (strncasecmp(Tag,St,Length) != 0)
	 continue;

      // Make sure the colon is in the right place
      const char *C = St + Length;
      for (; isspace(*C) != 0; C++);
      if (*C != ':')
	 continue;

      // Strip off the gunk from the start end
      Start = C;
      End = Section + Indexes[I+1];
      if (Start >= End)
	 return _error->Error("Internal parsing error");
      
      for (; (isspace(*Start) != 0 || *Start == ':') && Start < End; Start++);
      for (; isspace(End[-1]) != 0 && End > Start; End--);
      
      return true;
   }
   
   Start = End = 0;
   return false;
}
									/*}}}*/
// TagSection::FindS - Find a string					/*{{{*/
// ---------------------------------------------------------------------
/* */
string pkgTagSection::FindS(const char *Tag)
{
   const char *Start;
   const char *End;
   if (Find(Tag,Start,End) == false)
      return string();
   return string(Start,End);      
}
									/*}}}*/
// TagSection::FindI - Find an integer					/*{{{*/
// ---------------------------------------------------------------------
/* */
signed int pkgTagSection::FindI(const char *Tag,signed long Default)
{
   const char *Start;
   const char *Stop;
   if (Find(Tag,Start,Stop) == false)
      return Default;

   // Copy it into a temp buffer so we can use strtol
   char S[300];
   if ((unsigned)(Stop - Start) >= sizeof(S))
      return Default;
   strncpy(S,Start,Stop-Start);
   S[Stop - Start] = 0;
   
   char *End;
   signed long Result = strtol(S,&End,10);
   if (S == End)
      return Default;
   return Result;
}
									/*}}}*/
// TagSection::FindFlag - Locate a yes/no type flag			/*{{{*/
// ---------------------------------------------------------------------
/* The bits marked in Flag are masked on/off in Flags */
bool pkgTagSection::FindFlag(const char *Tag,unsigned long &Flags,
			     unsigned long Flag)
{
   const char *Start;
   const char *Stop;
   if (Find(Tag,Start,Stop) == false)
      return true;
   
   switch (StringToBool(string(Start,Stop)))
   {     
      case 0:
      Flags &= ~Flag;
      return true;

      case 1:
      Flags |= Flag;
      return true;

      default:
      _error->Warning("Unknown flag value");
      return true;
   }
   return true;
}
									/*}}}*/
