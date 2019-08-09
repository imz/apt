// Description								/*{{{*/
// $Id$
/* ######################################################################

RSYNC Aquire Method - This is the RSYNC aquire method for APT.

##################################################################### */
/*}}}*/
#ifndef APT_RSYNC_H
#define APT_RSYNC_H

using namespace std;

static const char * RSYNC_PROGRAM = "/usr/bin/rsync";

class Argv
{
   int max_size;
   int size;
   char **args;

  public:
   explicit Argv(int msize);
   ~Argv();

   Argv(const Argv &other) = delete;
   Argv& operator=(const Argv &other) = delete;

   bool add(const char *arg);
   bool add(const string &arg) { return add( arg.c_str()); }
   bool resize();
   int getSize() { return size; }
   operator char**() { return args; }
   operator string();
};


class RsyncMethod : public pkgAcqMethod
{
  protected:
   enum ConnType {ConnTypeExec, ConnTypeExecExt, ConnTypeProto};

   class RsyncConn
	  {
		public:
		 enum ConnState {Idle,Starting,Connecting,Fetching,Failed,Done};

		protected:
		 URI srv;
		 const string proxy;
		 ConnState State;

		 static char proxy_value[1024];
		 bool initProxy();

		public:
		 RsyncConn(URI u, const string &_proxy = ""): srv(u), proxy(_proxy) {}
		 virtual ~RsyncConn() {}

		 virtual bool Get(pkgAcqMethod *Owner, FetchResult &FRes, const char *From, const char *To) = 0;
	  };

   class RsyncConnExec: public RsyncConn
	  {
		 // pid of child process
		 pid_t ChildPid;
		 // output of child process (stdout&stderr)
		 int ChildFd;
		 // Program to execute
		 string program;

		protected:
		 bool WaitChild(pkgAcqMethod *Owner, FetchResult &FRes, const char *To);
		 virtual void ParseOutput(pkgAcqMethod *Owner, FetchResult &FRes, const char *buf);
		 virtual void AddOptions(Argv &argv)
			{ argv.add("-vvvv"); };

		public:
		 RsyncConnExec(URI u, const string &_proxy, const string &prog);
		 virtual ~RsyncConnExec();

		 virtual bool Get(pkgAcqMethod *Owner, FetchResult &FRes, const char *From, const char *To) override;
	  };

   class RsyncConnExecExt: public RsyncConnExec
	  {
		protected:
		 virtual void ParseOutput(pkgAcqMethod *Owner, FetchResult &FRes, const char *buf) override;
		 virtual void AddOptions(Argv &argv) override
			{ argv.add("--apt-support"); };

		public:
		 RsyncConnExecExt(URI u, const string &_proxy, const string &prog):
			RsyncConnExec(u, _proxy, prog) {};
	  };

   static RsyncConn *server;
   static ConnType connType;
   static bool Debug;
   static unsigned int Timeout;

   string RsyncProg;

   static void SigTerm(int);

  protected:
   virtual bool Fetch(FetchItem *Itm) override;
   virtual bool Configuration(const string &Message) override;

   void Start(FetchResult &FRes)
	  { URIStart(FRes); }

  public:
   
   RsyncMethod();
};

#endif
