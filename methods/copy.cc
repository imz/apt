// Description								/*{{{*/
// $Id: copy.cc,v 1.7 2003/02/10 07:34:41 doogie Exp $
/* ######################################################################

   Copy URI - This method takes a uri like a file: uri and copies it
   to the destination file.

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/fileutl_opt.h>
#include <apt-pkg/acquire-method.h>
#include <apt-pkg/error.h>
#include <apt-pkg/hashes.h>

#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>

// CNC:2003-02-20 - Moved header to fix compilation error when
// 		    --disable-nls is used.
#include <apti18n.h>
									/*}}}*/

class CopyMethod : public pkgAcqMethod
{
   virtual bool Fetch(FetchItem *Itm) override;

   public:

   CopyMethod() : pkgAcqMethod("1.0",SingleInstance) {}
};

// CopyMethod::Fetch - Fetch a file					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool CopyMethod::Fetch(FetchItem *Itm)
{
   URI Get = Itm->Uri;
   std::string const File = Get.Path;

   // Stat the file and send a start message
   struct stat Buf;
   if (stat(File.c_str(),&Buf) != 0)
      return _error->Errno("stat",_("Failed to stat"));

   // Forumulate a result and send a start message
   FetchResult Res;
   Res.Size = StSize(Buf);
   Res.Filename = Itm->DestFile;
   Res.LastModified = Buf.st_mtime;
   Res.IMSHit = false;
   URIStart(Res);

   // See if the file exists
   FileFd To(Itm->DestFile,FileFd::WriteEmpty);
   To.EraseOnFailure();
   if (_error->PendingError() == true)
   {
      To.OpFail();
      return false;
   }

   // Copy the file (and hash at the same time; simultaneous in-memory hashing
   // is common to all download methods)
   Hashes hash;
   if (! ConsumeFile(File,
                     [&To, &hash](const void * const Data, size_t const Count) -> bool
                     {
                        return To.Write(Data,Count)
                           && hash.Add(Data,Count);
                     }))
   {
      To.OpFail();
      return false;
   }
   To.Close();
   Res.TakeHashes(hash);

   // Transfer the modification times
   struct utimbuf TimeBuf;
   TimeBuf.actime = Buf.st_atime;
   TimeBuf.modtime = Buf.st_mtime;
   if (utime(Itm->DestFile.c_str(),&TimeBuf) != 0)
   {
      To.OpFail();
      return _error->Errno("utime",_("Failed to set modification time"));
   }

   URIDone(Res);
   return true;
}
									/*}}}*/

int main()
{
   CopyMethod Mth;
   return Mth.Run();
}
