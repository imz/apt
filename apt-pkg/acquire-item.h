// Description								/*{{{*/
// $Id: acquire-item.h,v 1.26 2003/02/02 03:13:13 doogie Exp $
/* ######################################################################

   Acquire Item - Item to acquire

   When an item is instantiated it will add it self to the local list in
   the Owner Acquire class. Derived classes will then call QueueURI to
   register all the URI's they wish to fetch at the initial moment.

   Two item classes are provided to provide functionality for downloading
   of Index files and downloading of Packages.

   A Archive class is provided for downloading .deb files. It does Md5
   checking and source location as well as a retry algorithm.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_ACQUIRE_ITEM_H
#define PKGLIB_ACQUIRE_ITEM_H

#include <apt-pkg/acquire.h>
#include <apt-pkg/indexfile.h>
#include <apt-pkg/pkgrecords.h>

// For the compatibility DoneByWorker() inline implementation.
// (Having it here as an inline function makes the complex idea
// of the compatibility methods more clear.)
#include <apt-pkg/strutl.h>

// Item to acquire
class pkgAcquire::Item
{
   protected:

   // Some private helper methods for registering URIs
   pkgAcquire *Owner;
   inline void QueueURI(ItemDesc &Item)
                 {Owner->Enqueue(Item);}
   inline void Dequeue() {Owner->Dequeue(this);}

   // Safe rename function with timestamp preservation
   void Rename(string From,string To);

   // The common actions to be re-used in subclasses in the implementations
   // of DoneByWorker() or of the analoguous deprecated Done()
   void BaseItem_Done(const string &Message,unsigned long Size,
                      const pkgAcquire::MethodConfig *Cnf /* unused for now */);

   // To be overridden for specialization of the action (by older subclasses).
   // Newer subclasses should override DoneByWorker() directly (which
   // corresponds to the new API of the worker given multiple types of cksums).
   //
   // It is also called by some older subclasses instead of calling
   // BaseItem_Done() directly.
   //
   // I.e., this deprecated method is present here for 2 different
   // compatibility reasons:
   // 1. to be overridden to provide specialized behavior for
   //    the DoneByWorker() action;
   // 2. to be called for the base handling of the action.
   virtual void Done(const string Message,
                     const unsigned long Size,
                     string /* Md5Hash unused in the base implementation */,
                     pkgAcquire::MethodConfig * const Cnf)
   { /* For compatibility with old subclasses whose implementations of Done()
        still call the base class's Item::Done() for the common actions.
        Newer subclasses should call BaseItem_Done() directly,
        because it is a cleaner API (namely, some unused parameters deleted).
     */
      BaseItem_Done(Message, Size, Cnf);
   }

   // For compatability: to be overridden by older subclasses who
   // do not know about ExpectedHash() and CheckType().
   //
   // FIXME: should be made const, but that's impossible due to compatibility
   // with older subclasses yet.
   virtual string MD5Sum() {return std::string();}

   public:

   // State of the item
   /* CNC:2002-11-22
    * Do not use anonyomus enums, as this breaks SWIG in some cases */
   enum StatusFlags {StatIdle, StatFetching, StatDone, StatError} Status;
   string ErrorText;

   /** \brief The size of the object to fetch. */
   unsigned long long FileSize;

   unsigned long PartialSize;
   const char *Mode;
   unsigned long ID;
   bool Complete;
   bool Local;

   // Number of queues we are inserted into
   unsigned int QueueCounter;

   // File to write the fetch into
   string DestFile;
   // Alternative temporary destination
   // Used if method (e.g. rsync) can't use directly DestFile
   string TmpFile;

   // Action members invoked by the worker
   // (they are usually overridden in subclasses for specialization)
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf);

   // new API (with a new name) that is public and called by the worker;
   // the old Done() method can be left protected for compatibility
   // with some old subclasses that override it (not in the apt project)
   virtual void DoneByWorker(const string &Message,
			     const unsigned long Size,
                             pkgAcquire::MethodConfig * const Cnf)
   { /* Older subclasses may have overridden Done() instead of this new method.
	For compatibility with such classes, we call their Done().
	Newer subclasses should override this method directly.
     */
      Done(Message,Size,LookupTag(Message,"MD5-Hash"),Cnf);
   }
   virtual void Start(string Message,unsigned long Size);
   virtual string Custom600Headers() {return string();}
   virtual string DescURI() = 0;
   virtual void Finished() {}

   // Inquire functions
   virtual string CheckType() const { return "MD5-Hash"; }
   // FIXME: should be made const, but that's not yet possible due to
   // compatibility with older subclasses (due to non-const MD5Sum()).
   virtual string ExpectedHash() { return MD5Sum(); /* compat with older subclasses */ }
   pkgAcquire *GetOwner() {return Owner;}

   Item(pkgAcquire *Owner);
   virtual ~Item();
};

// CNC:2002-07-03
class pkgRepository;

// Item class for index files
class pkgAcqIndex : public pkgAcquire::Item
{
   protected:

   bool Decompression;
   bool Erase;
   pkgAcquire::ItemDesc Desc;
   string RealURI;

   // CNC:2002-07-03
   const pkgRepository *Repository;

   /* Not implemented; therefore hidden as protected
      to prohibit meaningless direct calls on objects of this type.
   */
   virtual string CheckType() const override {return std::string();}
   virtual string ExpectedHash() override {return std::string();}

   public:

   // Specialized action members
   virtual void DoneByWorker(const string &Message,unsigned long Size,
                             pkgAcquire::MethodConfig *Cnf) override;
   virtual string Custom600Headers() override;
   virtual string DescURI() override {return RealURI;} // CNC:2003-02-14

   // CNC:2002-07-03
   pkgAcqIndex(pkgAcquire *Owner,const pkgRepository *Repository,string URI,
	       string URIDesc,string ShortDesct);
};

// Item class for index files
class pkgAcqIndexRel : public pkgAcquire::Item
{
   protected:

   pkgAcquire::ItemDesc Desc;
   string RealURI;

   // CNC:2002-07-03
   bool Authentication;
   bool Master;
   bool Erase;
   pkgRepository *Repository;

   /* Not implemented; therefore hidden as protected
      to prohibit meaningless direct calls on objects of this type.
   */
   virtual string CheckType() const override {return std::string();}
   virtual string ExpectedHash() override {return std::string();}

   public:

   // Specialized action members
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf) override;
   virtual void DoneByWorker(const string &Message,unsigned long Size,
                             pkgAcquire::MethodConfig *Cnf) override;
   virtual string Custom600Headers() override;
   virtual string DescURI() override {return RealURI;}

   // CNC:2002-07-03
   pkgAcqIndexRel(pkgAcquire *Owner,pkgRepository *Repository,string URI,
		  string URIDesc,string ShortDesc,bool Master=false);
};

// Item class for archive files
class pkgAcqArchive : public pkgAcquire::Item
{
   protected:

   // State information for the retry mechanism
   pkgCache::VerIterator Version;
   pkgAcquire::ItemDesc Desc;
   const pkgSourceList *Sources;
   pkgRecords *Recs;
   string ExpectHash;
   string ChkType;
   string &StoreFilename;
   pkgCache::VerFileIterator Vf;
   unsigned int Retries;

   // Queue the next available file for download.
   bool QueueNext();

   public:

   // Specialized action members
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf) override;
   virtual void DoneByWorker(const string &Message,unsigned long Size,
                             pkgAcquire::MethodConfig *Cnf) override;
   virtual string CheckType() const override {return ChkType;}
   virtual string ExpectedHash() override {return ExpectHash;}
   virtual string DescURI() override {return Desc.URI;}
   virtual void Finished() override;

   pkgAcqArchive(pkgAcquire *Owner,const pkgSourceList *Sources,
		 pkgRecords *Recs,pkgCache::VerIterator const &Version,
		 string &StoreFilename);
};

// Fetch a generic file to the current directory
class pkgAcqFile : public pkgAcquire::Item
{
   pkgAcquire::ItemDesc Desc;
   string ExpectMd5Hash;
   unsigned int Retries;

   public:

   // Specialized action members
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf) override;
   virtual void DoneByWorker(const string &Message,unsigned long Size,
                             pkgAcquire::MethodConfig *Cnf) override;
   virtual string MD5Sum() override {return ExpectMd5Hash;}
   virtual string DescURI() override {return Desc.URI;}

   // If DestFilename is empty, download to DestDir/<basename> if
   // DestDir is non-empty, $CWD/<basename> otherwise.  If
   // DestFilename is NOT empty, DestDir is ignored and DestFilename
   // is the absolute name to which the file should be downloaded.
   pkgAcqFile(pkgAcquire *Owner, string URI, string MD5, unsigned long Size,
	      string Desc, string ShortDesc,
	      const string &DestDir="", const string &DestFilename="");
};

#endif
