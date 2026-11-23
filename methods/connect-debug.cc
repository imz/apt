// Description								/*{{{*/
/* ######################################################################

   Wrapping of MethodFd (for Connect) and its debugging by wrapping

   ##################################################################### */
									/*}}}*/
#include <config.h>

#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/configuration.h>

#include <cassert>
#include <sys/time.h>
#include <fcntl.h>

#include "connect-debug.h"

#include <apti18n.h>

// not in header because of FileFd dep
struct DebuggedMethodFd: TracedMethodFd
{
   FileFd LogFd;

   DebuggedMethodFd(std::unique_ptr<MethodFd> &MFd, const std::string &FileName);

   bool Log(const std::string &s) { return LogFd.Write(s.c_str(), s.length()); }

   protected:
   bool Fd_return(int res) override;
   bool Read_return(ssize_t res, const void *buf) override;
   bool Write_return(ssize_t res, const void *buf) override;
   bool Close_return(int res) override;

   bool Read_enter(size_t count) override;
   bool Write_enter(const void * buf, size_t count) override;
   bool Close_enter() override;
};

TracedMethodFd::TracedMethodFd(std::unique_ptr<MethodFd> &MFd):
   WMethodFd(MFd)
{}

int TracedMethodFd::Fd()
{
   auto const Res = WMethodFd::Fd();
   assert(Fd_return(Res));
   return Res;
}

ssize_t TracedMethodFd::Read(void * const buf, size_t const count)
{
   assert(Read_enter(count));
   auto const Res = WMethodFd::Read(buf,count);
   int const errsv = errno;
   assert(Read_return(Res, buf));
   errno = errsv;
   return Res;
}

ssize_t TracedMethodFd::Write(const void * const buf, size_t const count)
{
   assert(Write_enter(buf,count));
   auto const Res = WMethodFd::Write(buf,count);
   int const errsv = errno;
   assert(Write_return(Res, buf));
   errno = errsv;
   return Res;
}

int TracedMethodFd::Close()
{
   assert(Close_enter());
   auto const Res = WMethodFd::Close();
   int const errsv = errno;
   assert(Close_return(Res));
   errno = errsv;
   return Res;
}

// Not much interesting data here output by debugging, but the fact that Fd()
// was called can be a meaningful debugging info, because it would mean
// that some operations could be done at this place directly with the fd,
// without our debugging tracing them.
bool DebuggedMethodFd::Fd_return(int const res)
{
   std::string const s = "\nFd() = " + std::to_string(res) + "\n";
   return Log(s);
}

bool DebuggedMethodFd::Read_return(ssize_t const res,
                                   const void * const buf)
{
   std::string const s = "\nRead() = " + std::to_string(res) + "\n";
   return Log(s)
      && (res <= 0 || LogFd.Write(buf, res));
}

bool DebuggedMethodFd::Write_return(ssize_t const res,
                                    const void * const buf)
{
   std::string const s = "\nWrite() = " + std::to_string(res) + "\n";
   return Log(s)
      && (res <= 0 || LogFd.Write(buf, res));
}

bool DebuggedMethodFd::Close_return(int const res)
{
   std::string const s = "\nClose() = " + std::to_string(res) + "\n";
   return Log(s);
}

bool DebuggedMethodFd::Read_enter(size_t const count)
{
   std::string const s = "\nRead(" + std::to_string(count) + ") ...\n";
   return Log(s);
}

bool DebuggedMethodFd::Write_enter(const void * const buf, /* not logged */
                                   size_t const count)
{
   std::string const s = "\nWrite(" + std::to_string(count) + ") ...\n";
   return Log(s);
}

bool DebuggedMethodFd::Close_enter()
{
   std::string const s = "\nClose() ...\n";
   return Log(s);
}

DebuggedMethodFd::DebuggedMethodFd(std::unique_ptr<MethodFd> &MFd,
                                   const std::string &FileName)
   : TracedMethodFd(MFd),
     LogFd(FileName, FileFd::WriteTemp /* implies O_EXCL */, S_IRUSR)
     /* O_EXCL is a trivial way to avoid collisions/loss of output,
        or interference with unowned files.
        S_IRUSR is to protect secret data such as auth (especially appropriate
        if we are run set-UID and a user could control the output location).
     */
{}

bool DebugMethodFdToFile(const std::string &FileName,
                         std::unique_ptr<MethodFd> &MFd)
{
   std::unique_ptr<DebuggedMethodFd> newFd(new DebuggedMethodFd(MFd,FileName));

   if (! newFd->LogFd.IsOpen())
   {
      MFd = std::move(newFd->UnderlyingFd);

      return false;
   }

   MFd = std::move(newFd);
   return true;
}

bool DebugMethodFd(const std::string &LogDir, std::unique_ptr<MethodFd> &MFd)
{
   struct timeval Time;
   gettimeofday(&Time,0);
   return
      DebugMethodFdToFile(LogDir
                          + "/" + std::to_string(Time.tv_sec)
                          + "." + std::to_string(Time.tv_usec) // FIXME: fixed width
                          + "." + QuoteString(MFd->Label(), "/"),
                          MFd);
}

bool DebugMethodFdIfRequired(std::unique_ptr<MethodFd> &MFd)
{
   // FindDir never returns an empty string, so we can't use it as an indicator.
   std::string const LogDir = _config->FindFile("Debug::Connect");
   if (! LogDir.empty())
   {
      if (! DebugMethodFd(LogDir, MFd))
      {
         // A failure to set up debugging is not a connection error,
         // so don't report it as such, i.e., don't return false.
         _error->Warning(_("Could not set up debugging for "
                           "the connection %s"),
                         MFd->Label().c_str());

         // Note that an errno-based explanation was already put into _error.

         // FIXME: We want that a failure to set up debugging doesn't go unnoticed,
         // however it wouldn't be correct to treat it as a connection error
         // by the calling code. So, we just bail out... (Could be an option.)
         //
         // Not to repeat the same exit code every time this function is used,
         // we "factor out" the dying code from those places to this single place.
         const bool FatalDebugMethodFd = true;
         if (FatalDebugMethodFd)
         {
            _error->DumpErrors();
            std::cerr << "FATAL -> failed to set up debugging of MethodFd"
                      << std::endl;
            exit(100);
         }
      }
   }

   return true;
}
