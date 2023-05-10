// Description								/*{{{*/
/* ######################################################################

   Additional File Utilities

   We couldn't place these ones into fileutl.h, because they use
   std::optional in their API, and not all clients of the library
   compile with -std=c++17, which is needed for it to be available.

   GetFileSize - Return the size of a file by path or report it missing

   These are handy abstractions for functions accepting filenames and
   they follow the APT representations of the types (i.e., size is unsigned).

   This source is placed in the Public Domain (as the original fileutl.h),
   do with it what you will.
   This addendum to fileutl.h was originally written by Ivan Zakharyaschev.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_FILEUTL_OPT_H
#define PKGLIB_FILEUTL_OPT_H

#include <apt-pkg/fileutl.h>
// for operations on filesize (it also uses C++17 lib)
#include <apt-pkg/arithutl.h>

#include <optional>

// for inline functions and templates
#include <sys/stat.h>
#include <memory>

// a helper for FileFd::Size()-like functions
constexpr filesize StSize(const struct stat &Buf)
{
   // the common safest way is to first cast to the same-width unsigned type
   return filesize{NonnegAsU(Buf.st_size)};

   /* Note that this form (filesize{...}) under -Werror=narrowing
      would keep us from:
      (1) initializing an unsigned type from a signed value;
      (2) initializing from a wider type.

      Good: (2) would keep us from loosing information, and (1) would make
      us pay special attention to casting and zero-extending signed vars.
      (I believe (1) is not a real concern here for a non-negative value,
      but in general one should first cast to the same-width unsigned type.)

      And if filesize is a scoped enum (enum class), this is the
      only form of initializing it from an integer, so then we'd be
      forced not to make this kind of narrowing errors (including
      sign-extension) with other forms of initilization.
   */
}

std::optional<filesize> GetFileSize(const std::string &File);

// Consume - Buffered feeding of data read from a file to a consumer	 *{{{*/
// ---------------------------------------------------------------------
/* Consumer is expected behave similarly to FileFd::Write(). Thanks to this
   being a template, various function-like types of consumers are allowed.
*/
template<typename consumer_t>
bool Consume(FileFd &From,consumer_t Consumer,filesize Size)
{
   if (From.IsOpen() == false)
      return false;

   // Buffered copy
   constexpr std::size_t Buf_size{64*1024};
   std::unique_ptr<unsigned char[]> const Buf(new unsigned char[Buf_size]);
   if (! Buf)
      return false;
   while (Size != filesize{0})
   {
      std::size_t ToRead;
      if (! SafeAssign_u(ToRead,Size) || ToRead > Buf_size)
         ToRead = Buf_size;

      if (From.Read(Buf.get(),ToRead) == false ||
	  Consumer(Buf.get(),ToRead) == false)
	 return false;

      // Considering reading+writing too much is a failure. Anyway, in such
      // case, the condition of the loop won't stop us after a overflow.
      if (! NonnegSubtract_u(Size, ToRead))
         return false;
   }

   return true;
}
									/*}}}*/
// ConsumeWhole - Consume a whole file (from the beginning)		 *{{{*/
// ---------------------------------------------------------------------
/* Consumer is expected behave similarly to FileFd::Write(). Thanks to this
   being a template, various function-like types of consumers are allowed.
*/
template<typename consumer_t>
bool ConsumeWhole(FileFd &From,consumer_t Consumer)
{
   if (From.IsOpen() == false)
      return false;

   // Consuming the whole size of the file makes sense
   // if we start from the very beginning
   if (! From.Seek(filesize{0}))
      return false;
   return Consume(From,Consumer,From.Size());
}
									/*}}}*/
// ConsumeFile - Consume a whole file given by name			 *{{{*/
// ---------------------------------------------------------------------
/* Consumer is expected behave similarly to FileFd::Write(). Thanks to this
   being a template, various function-like types of consumers are allowed.
*/
template<typename consumer_t>
bool ConsumeFile(const std::string &File,consumer_t Consumer)
{
   FileFd From(File, FileFd::ReadOnly);
   // Consuming the whole size of the file makes sense
   // if we've just opened it and hence start from the beginning.
   return Consume(From,Consumer,From.Size());
}
									/*}}}*/

#endif
