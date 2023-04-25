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
#include <apt-pkg/fileutl.h>

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
   void BaseItem_Done(const string &Message,filesize Size,
                      const pkgAcquire::MethodConfig *Cnf /* unused for now */);

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
   filesize FileSize;

   filesize PartialSize;
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
   // instead of the old Done() method
   virtual void DoneByWorker(const string &Message,
			     filesize Size,
                             pkgAcquire::MethodConfig * const Cnf) = 0;
   virtual void Start(string Message,filesize Size);
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
   virtual void DoneByWorker(const string &Message,filesize Size,
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
   virtual void DoneByWorker(const string &Message,filesize Size,
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
   virtual void DoneByWorker(const string &Message,filesize Size,
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
   virtual void DoneByWorker(const string &Message,filesize Size,
                             pkgAcquire::MethodConfig *Cnf) override;
   virtual string MD5Sum() override {return ExpectMd5Hash;}
   virtual string DescURI() override {return Desc.URI;}

   pkgAcqFile(pkgAcquire *Owner,string URI,string MD5,filesize Size,
		  string Desc,string ShortDesc);
};

#endif
