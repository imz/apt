// Description								/*{{{*/
// $Id: connect.cc,v 1.10 2003/02/10 07:34:41 doogie Exp $
/* ######################################################################

   Connect - Replacement connect call

   This was originally authored by Jason Gunthorpe <jgg@debian.org>
   and is placed in the Public Domain, do with it what you will.
      
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include "connect.h"
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>

#include <stdio.h>
#include <errno.h>
#include <unistd.h>

// Internet stuff
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifdef USE_TLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#endif /* USE_TLS */

// CNC:2003-02-20 - Moved header to fix compilation error when
// 		    --disable-nls is used.
#include <apti18n.h>

#include "rfc2553emu.h"
									/*}}}*/

static string LastHost;
static int LastPort = 0;
static struct addrinfo *LastHostAddr = 0;
static struct addrinfo *LastUsed = 0;

// File Descriptor based Fd /*{{{*/
struct FdFd: public MethodFd
{
   int fd = -1;

   int Fd() override { return fd; }
   ssize_t Read(void *buf, size_t count) override { return ::read(fd, buf, count); }
   ssize_t Write(const void *buf, size_t count) override { return ::write(fd, buf, count); }
   int Close() override
   {
      int result = 0;
      if (fd != -1)
         result = ::close(fd);
      fd = -1;
      return result;
   }
};

bool MethodFd::HasPending()
{
   return false;
}

std::unique_ptr<MethodFd> MethodFd::FromFd(int iFd)
{
   FdFd *fd = new FdFd();
   fd->fd = iFd;
   return std::unique_ptr<MethodFd>(fd);
}

// RotateDNS - Select a new server from a DNS rotation			/*{{{*/
// ---------------------------------------------------------------------
/* This is called during certain errors in order to recover by selecting a 
   new server */
void RotateDNS()
{
   if (LastUsed != 0 && LastUsed->ai_next != 0)
      LastUsed = LastUsed->ai_next;
   else
      LastUsed = LastHostAddr;
}
									/*}}}*/
// DoConnect - Attempt a connect operation				/*{{{*/
// ---------------------------------------------------------------------
/* This helper function attempts a connection to a single address. */
static bool DoConnect(struct addrinfo *Addr,string Host,
		      unsigned long TimeOut,std::unique_ptr<MethodFd> &Fd,pkgAcqMethod *Owner)
{
   // Show a status indicator
   char Name[NI_MAXHOST];
   char Service[NI_MAXSERV];
   
   Name[0] = 0;   
   Service[0] = 0;
   getnameinfo(Addr->ai_addr,Addr->ai_addrlen,
	       Name,sizeof(Name),Service,sizeof(Service),
	       NI_NUMERICHOST|NI_NUMERICSERV);
   Owner->Status(_("Connecting to %s (%s)"),Host.c_str(),Name);

   /* If this is an IP rotation store the IP we are using.. If something goes
      wrong this will get tacked onto the end of the error message */
   if (LastHostAddr->ai_next != 0)
   {
      char Name2[NI_MAXHOST + NI_MAXSERV + 10];
      snprintf(Name2,sizeof(Name2),_("[IP: %s %s]"),Name,Service);
      Owner->SetFailExtraMsg(string(Name2));
   }   
   else
      Owner->SetFailExtraMsg("");
      
   // Get a socket
   Fd = MethodFd::FromFd(socket(Addr->ai_family,Addr->ai_socktype, Addr->ai_protocol));
   if (Fd->Fd() < 0)
      return _error->Errno("socket",_("Could not create a socket for %s (f=%u t=%u p=%u)"),
			   Name,Addr->ai_family,Addr->ai_socktype,Addr->ai_protocol);
   
   SetNonBlock(Fd->Fd(),true);
   if (connect(Fd->Fd(),Addr->ai_addr,Addr->ai_addrlen) < 0 &&
       errno != EINPROGRESS)
      return _error->Errno("connect",_("Cannot initiate the connection "
			   "to %s:%s (%s)."),Host.c_str(),Service,Name);
   
   /* This implements a timeout for connect by opening the connection
      nonblocking */
   if (WaitFd(Fd->Fd(),true,TimeOut) == false)
      return _error->Error(_("Could not connect to %s:%s (%s), "
			   "connection timed out"),Host.c_str(),Service,Name);

   // Check the socket for an error condition
   unsigned int Err;
   unsigned int Len = sizeof(Err);
   if (getsockopt(Fd->Fd(),SOL_SOCKET,SO_ERROR,&Err,&Len) != 0)
      return _error->Errno("getsockopt",_("Failed"));
   
   if (Err != 0)
   {
      errno = Err;
      return _error->Errno("connect",_("Could not connect to %s:%s (%s)."),Host.c_str(),
			   Service,Name);
   }
   
   return true;
}
									/*}}}*/
// Connect - Connect to a server					/*{{{*/
// ---------------------------------------------------------------------
/* Performs a connection to the server */
bool Connect(string Host,int Port,const char *Service,int DefPort,std::unique_ptr<MethodFd> &Fd,
	     unsigned long TimeOut,pkgAcqMethod *Owner)
{
   if (_error->PendingError() == true)
      return false;

   // Convert the port name/number
   char ServStr[300];
   if (Port != 0)
      snprintf(ServStr,sizeof(ServStr),"%u",Port);
   else
      snprintf(ServStr,sizeof(ServStr),"%s",Service);
   
   /* We used a cached address record.. Yes this is against the spec but
      the way we have setup our rotating dns suggests that this is more
      sensible */
   if (LastHost != Host || LastPort != Port)
   {
      Owner->Status(_("Connecting to %s"),Host.c_str());

      // Free the old address structure
      if (LastHostAddr != 0)
      {
	 freeaddrinfo(LastHostAddr);
	 LastHostAddr = 0;
	 LastUsed = 0;
      }
      
      // We only understand SOCK_STREAM sockets.
      struct addrinfo Hints;
      memset(&Hints,0,sizeof(Hints));
      Hints.ai_socktype = SOCK_STREAM;
      Hints.ai_protocol = 0;
      
      // Resolve both the host and service simultaneously
      while (1)
      {
	 int Res;
	 if ((Res = getaddrinfo(Host.c_str(),ServStr,&Hints,&LastHostAddr)) != 0 ||
	     LastHostAddr == 0)
	 {
	    if (Res == EAI_NONAME || Res == EAI_SERVICE)
	    {
	       if (DefPort != 0)
	       {
		  snprintf(ServStr,sizeof(ServStr),"%u",DefPort);
		  DefPort = 0;
		  continue;
	       }
	       return _error->Error(_("Could not resolve '%s'"),Host.c_str());
	    }
	    
	    if (Res == EAI_AGAIN)
	       return _error->Error(_("Temporary failure resolving '%s'"),
				    Host.c_str());
	    return _error->Error(_("Something wicked happened resolving '%s:%s' (%i)"),
				 Host.c_str(),ServStr,Res);
	 }
	 break;
      }
      
      LastHost = Host;
      LastPort = Port;
   }

   // When we have an IP rotation stay with the last IP.
   struct addrinfo *CurHost = LastHostAddr;
   if (LastUsed != 0)
       CurHost = LastUsed;
   
   while (CurHost != 0)
   {
      if (DoConnect(CurHost,Host,TimeOut,Fd,Owner) == true)
      {
	 LastUsed = CurHost;
	 return true;
      }      
      if (Fd)
         Fd->Close();
      Fd.reset();
      
      // Ignore UNIX domain sockets
      do
      {
	 CurHost = CurHost->ai_next;
      }
      while (CurHost != 0 && CurHost->ai_family == AF_UNIX);

      /* If we reached the end of the search list then wrap around to the
         start */
      if (CurHost == 0 && LastUsed != 0)
	 CurHost = LastHostAddr;
      
      // Reached the end of the search cycle
      if (CurHost == LastUsed)
	 break;
      
      if (CurHost != 0)
	 _error->Discard();
   }   

   if (_error->PendingError() == true)
      return false;   
   return _error->Error(_("Unable to connect to %s %s:"),Host.c_str(),ServStr);
}
									/*}}}*/

#ifdef USE_TLS
// UnwrapTLS - Handle TLS connections 					/*{{{*/
// ---------------------------------------------------------------------
/* Performs a TLS handshake on the socket */
struct TlsFd : public MethodFd
{
   std::unique_ptr<MethodFd> UnderlyingFd;
   gnutls_session_t session;
   gnutls_certificate_credentials_t credentials;
   std::string hostname;
   bool session_need_shutdown;

   int Fd() override { return UnderlyingFd->Fd(); }

   ssize_t Read(void *buf, size_t count) override
   {
      return HandleError(gnutls_record_recv(session, buf, count));
   }
   ssize_t Write(const void *buf, size_t count) override
   {
      return HandleError(gnutls_record_send(session, buf, count));
   }

   template <typename T>
   T HandleError(T err)
   {
      if (err < 0 && gnutls_error_is_fatal(err))
	 errno = EIO;
      else if (err < 0)
	 errno = EAGAIN;
      else
	 errno = 0;
      return err;
   }

   int Close() override
   {
      int err = 0;

      if (session)
      {
         if (session_need_shutdown)
            err = HandleError(gnutls_bye(session, GNUTLS_SHUT_RDWR));
         if (credentials)
         {
            gnutls_certificate_free_credentials(credentials);
            credentials = nullptr;
         }
         gnutls_deinit(session);
         session = nullptr;
      }

      int lower = (UnderlyingFd ? UnderlyingFd->Close() : 0);
      return (err < 0) ? HandleError(err) : lower;
   }

   bool HasPending() override
   {
      return gnutls_record_check_pending(session) > 0;
   }
};

bool UnwrapTLS(const std::string &Host, std::unique_ptr<MethodFd> &Fd,
		      unsigned long Timeout, pkgAcqMethod *Owner)
{
   int err;
   TlsFd *tlsFd = nullptr;

   // Set the FD now, so closing it works reliably.
   {
      std::unique_ptr<TlsFd> tlsFdUnique(new TlsFd());
      tlsFdUnique->hostname = Host;
      tlsFdUnique->UnderlyingFd = std::move(Fd);

      tlsFd = tlsFdUnique.get();
      Fd = std::move(tlsFdUnique);
   }

   if ((err = gnutls_init(&tlsFd->session, GNUTLS_CLIENT | GNUTLS_NONBLOCK)) < 0)
   {
      _error->Error(_("Internal error: could not allocate credentials: %s"), gnutls_strerror(err));
      return false;
   }

   FdFd *fdfd = dynamic_cast<FdFd *>(tlsFd->UnderlyingFd.get());
   if (fdfd != nullptr)
   {
      gnutls_transport_set_int(tlsFd->session, fdfd->fd);
   }
   else
   {
      gnutls_transport_set_ptr(tlsFd->session, tlsFd->UnderlyingFd.get());
      gnutls_transport_set_pull_function(tlsFd->session,
					 [](gnutls_transport_ptr_t p, void *buf, size_t size) -> ssize_t {
					    return reinterpret_cast<MethodFd *>(p)->Read(buf, size);
					 });
      gnutls_transport_set_push_function(tlsFd->session,
					 [](gnutls_transport_ptr_t p, const void *buf, size_t size) -> ssize_t {
					    return reinterpret_cast<MethodFd *>(p)->Write(buf, size);
					 });
   }

   if ((err = gnutls_certificate_allocate_credentials(&tlsFd->credentials)) < 0)
   {
      _error->Error(_("Internal error: could not allocate credentials: %s"), gnutls_strerror(err));
      return false;
   }

   // Credential setup
   std::string fileinfo = _config->Find("Acquire::https::CaInfo");
   if (fileinfo.empty())
   {
      // No CaInfo specified, use system trust store.
      err = gnutls_certificate_set_x509_system_trust(tlsFd->credentials);
      if (err == 0)
	 Owner->Warning(_("No system certificates available. Try installing ca-certificates."));
      else if (err < 0)
      {
	 _error->Error(_("Could not load system TLS certificates: %s"), gnutls_strerror(err));
	 return false;
      }
   }
   else
   {
      // CA location has been set, use the specified one instead
      gnutls_certificate_set_verify_flags(tlsFd->credentials, GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT);
      err = gnutls_certificate_set_x509_trust_file(tlsFd->credentials, fileinfo.c_str(), GNUTLS_X509_FMT_PEM);
      if (err < 0)
      {
	 _error->Error(_("Could not load certificates from %s (CaInfo option): %s"), fileinfo.c_str(), gnutls_strerror(err));
	 return false;
      }
   }

   // For client authentication, certificate file ...
   std::string const cert = _config->Find("Acquire::https::SslCert");
   std::string const key = _config->Find("Acquire::https::SslKey");
   if (!cert.empty())
   {
      if ((err = gnutls_certificate_set_x509_key_file(
	       tlsFd->credentials,
	       cert.c_str(),
	       key.empty() ? cert.c_str() : key.c_str(),
	       GNUTLS_X509_FMT_PEM)) < 0)
      {
	 _error->Error(_("Could not load client certificate (%s, SslCert option) or key (%s, SslKey option): %s"), cert.c_str(), key.c_str(), gnutls_strerror(err));
	 return false;
      }
   }

   // CRL file
   std::string const crlfile = _config->Find("Acquire::https::CrlFile");
   if (!crlfile.empty())
   {
      if ((err = gnutls_certificate_set_x509_crl_file(tlsFd->credentials,
						      crlfile.c_str(),
						      GNUTLS_X509_FMT_PEM)) < 0)
      {
	 _error->Error(_("Could not load custom certificate revocation list %s (CrlFile option): %s"), crlfile.c_str(), gnutls_strerror(err));
	 return false;
      }
   }

   if ((err = gnutls_credentials_set(tlsFd->session, GNUTLS_CRD_CERTIFICATE, tlsFd->credentials)) < 0)
   {
      _error->Error(_("Internal error: Could not add certificates to session: %s"), gnutls_strerror(err));
      return false;
   }

   if ((err = gnutls_set_default_priority(tlsFd->session)) < 0)
   {
      _error->Error(_("Internal error: Could not set algorithm preferences: %s"), gnutls_strerror(err));
      return false;
   }

   if (_config->FindB("Acquire::https::Verify-Peer", true))
   {
      gnutls_session_set_verify_cert(tlsFd->session, _config->FindB("Acquire::https::Verify-Host", true) ? tlsFd->hostname.c_str() : nullptr, 0);
   }

   // set SNI only if the hostname is really a name and not an address
   {
      struct in_addr addr4;
      struct in6_addr addr6;

      if (inet_pton(AF_INET, tlsFd->hostname.c_str(), &addr4) == 1 ||
	  inet_pton(AF_INET6, tlsFd->hostname.c_str(), &addr6) == 1)
	 /* not a host name */;
      else if ((err = gnutls_server_name_set(tlsFd->session, GNUTLS_NAME_DNS, tlsFd->hostname.c_str(), tlsFd->hostname.length())) < 0)
      {
	 _error->Error(_("Could not set host name %s to indicate to server: %s"), tlsFd->hostname.c_str(), gnutls_strerror(err));
	 return false;
      }
   }

   // Do the handshake. Our socket is non-blocking, so we need to call WaitFd()
   // accordingly.
   do
   {
      err = gnutls_handshake(tlsFd->session);
      if ((err == GNUTLS_E_INTERRUPTED || err == GNUTLS_E_AGAIN) &&
	  WaitFd(Fd->Fd(), gnutls_record_get_direction(tlsFd->session) == 1, Timeout) == false)
      {
	 _error->Errno("select", _("Could not wait for server fd"));
	 return false;
      }
   } while (err < 0 && gnutls_error_is_fatal(err) == 0);

   tlsFd->session_need_shutdown = true;

   if (err < 0)
   {
      // Print reason why validation failed.
      if (err == GNUTLS_E_CERTIFICATE_VERIFICATION_ERROR)
      {
	 gnutls_datum_t txt;
	 auto type = gnutls_certificate_type_get(tlsFd->session);
	 auto status = gnutls_session_get_verify_cert_status(tlsFd->session);
	 if (gnutls_certificate_verification_status_print(status,
							  type, &txt, 0) == 0)
	 {
	    _error->Error(_("Certificate verification failed: %s"), txt.data);
	 }
	 gnutls_free(txt.data);
      }
      _error->Error("Could not handshake: %s", gnutls_strerror(err));
      return false;
   }

   return true;
}
									/*}}}*/
#endif /* USE_TLS */
