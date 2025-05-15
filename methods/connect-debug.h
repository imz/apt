// Description								/*{{{*/
/* ######################################################################

   Wrapping of MethodFd (for Connect) and its debugging by wrapping

   ##################################################################### */

/*
A wrapper class is a more convenient and appropriate tool for
debugging connections in APT than an external utility like strace
because there can be several layers of connections, all abstracted by
MethodFd, e.g., inside TLS, TLS inside TLS to proxy, etc.

The kind of bugs that depend on the amound of bytes read() etc. happen
randomly and often rarely. (It's difficult to catch the same one or
somehow reproduce it deterministically.)

TODO: One approach to reproduce them might be to use a wrapper class
around MethodFd (like this wrapper for debugging and just outputting
the operations), which would inject the Read() responses replaying the
saved log from DebugMethodFd.
*/
									/*}}}*/
#ifndef CONNECT_DEBUG_H
#define CONNECT_DEBUG_H

#include "connect.h"

bool DebugMethodFdToFile(const string &FileName,
                         std::unique_ptr<MethodFd> &MFd);
bool DebugMethodFd(const string &LogDir, std::unique_ptr<MethodFd> &MFd);
bool DebugMethodFdIfRequired(std::unique_ptr<MethodFd> &MFd);

/**
 * Wrapped MethodFd
 */
struct WMethodFd: MethodFd
{
   // It's invalid to use this object if UnderlyingFd isn't set to a real object.
   // However, it might be reasonable at some places to "unwrap" UnderlyingFd
   // and continue operating with it directly, thereby leaving an "invalid"
   // WrappedMethodFd. (But this shortcoming is like this for unique_ptr, too.)
   std::unique_ptr<MethodFd> UnderlyingFd;

   explicit WMethodFd(std::unique_ptr<MethodFd> &MFd): UnderlyingFd(std::move(MFd)) {}

   int Fd() override { return UnderlyingFd->Fd(); }
   ssize_t Read(void * const buf, size_t const count) override { return UnderlyingFd->Read(buf,count); }
   ssize_t Write(const void * const buf, size_t const count) override { return UnderlyingFd->Write(buf,count); }
   int Close() override { return UnderlyingFd->Close(); }
   bool HasPending() override { return UnderlyingFd->HasPending(); }
   std::string Label() override { return UnderlyingFd->Label(); }
};

struct TracedMethodFd: WMethodFd
{
   explicit TracedMethodFd(std::unique_ptr<MethodFd> &MFd);

   int Fd() override;
   ssize_t Read(void * const buf, size_t const count) override;
   ssize_t Write(const void * const buf, size_t const count) override;
   int Close() override;

   protected:
   virtual bool Fd_return(int res) = 0;
   virtual bool Read_return(ssize_t res, const void *buf) = 0;
   virtual bool Write_return(ssize_t res, const void *buf) = 0;
   virtual bool Close_return(int res) = 0;

   virtual bool Read_enter(size_t count) = 0;
   virtual bool Write_enter(const void * buf, size_t count) = 0;
   virtual bool Close_enter() = 0;
};

#endif
