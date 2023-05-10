// Description								/*{{{*/
// $Id: file.cc,v 1.9 2003/02/10 07:34:41 doogie Exp $
/* ######################################################################

   File URI method for APT

   This simply checks that the file specified exists, if so the relevent
   information is returned. If a .gz filename is specified then the file
   name with .gz removed will also be checked and information about it
   will be returned in Alt-*

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/acquire-method.h>
#include <apt-pkg/error.h>
#include <apt-pkg/hashes.h>
#include <apt-pkg/fileutl_opt.h>

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

// CNC:2003-02-20 - Moved header to fix compilation error when
// 		    --disable-nls is used.
#include <apti18n.h>
									/*}}}*/

class FileMethod : public pkgAcqMethod
{
   virtual bool Fetch(FetchItem *Itm) override;

   public:

   FileMethod() : pkgAcqMethod("1.0",SingleInstance | LocalOnly) {}
};

// FileMethod::Fetch - Fetch a file					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool FileMethod::Fetch(FetchItem *Itm)
{
   URI Get = Itm->Uri;
   string File = Get.Path;
   FetchResult Res;
   if (Get.Host.empty() == false)
      return _error->Error(_("Invalid URI, local URIS must not start with //"));

   // See if the file exists
   struct stat Buf;
   if (stat(File.c_str(),&Buf) == 0)
   {
      Res.Size = StSize(Buf);
      Res.Filename = File;
      Res.LastModified = Buf.st_mtime;
      Res.IMSHit = false;
      if (Itm->LastModified == Buf.st_mtime && Itm->LastModified != 0)
	 Res.IMSHit = true;

      Hashes hash;
      hash.AddFile(File);
      Res.TakeHashes(hash);
   }

   // CNC:2003-11-04
   // See if we can compute a file without a .gz/.bz2/etc extension
   //
   // FIXME: For this to work, the extension tried here must match the
   // extension in the item queued from the APT process. The bad thing
   // seems to be that this download method doesn't get this
   // configuration variable as initialized in the APT process; so,
   // there can be a mismatch. The situation doesn't look so bad if
   // several extensions are tried in a sequence when queuing: if at
   // least one of them matches the only extension known by this
   // method, then this trick will work. However, e.g., if ".xz" is
   // queued in the first try, and the xz-compressed file actually exists,
   // then if this method doesn't know about ".xz", it won't supply
   // the uncompressed file in the reply, so we shall then waste time
   // decompressing instead of optimally taking the uncompressed file
   // (if it exists).
   string ComprExtension = _config->Find("Acquire::ComprExtension", ".bz2");
   string::size_type Pos = File.rfind(ComprExtension);
   if (Pos + ComprExtension.length() == File.length())
   {
      File = string(File,0,Pos);
      if (stat(File.c_str(),&Buf) == 0)
      {
	 FetchResult AltRes;
	 AltRes.Size = StSize(Buf);
	 AltRes.Filename = File;
	 AltRes.LastModified = Buf.st_mtime;
	 AltRes.IMSHit = false;
	 if (Itm->LastModified == Buf.st_mtime && Itm->LastModified != 0)
	    AltRes.IMSHit = true;

	 URIDone(Res,&AltRes);
	 return true;
      }
   }

   if (Res.Filename.empty() == true)
      return _error->Error(_("File not found"));

   URIDone(Res);
   return true;
}
									/*}}}*/

int main()
{
   FileMethod Mth;
   return Mth.Run();
}
