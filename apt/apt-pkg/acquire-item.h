// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: acquire-item.h,v 1.6 2001/11/12 16:34:00 kojima Exp $
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
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/pkgrecords.h>

#ifdef __GNUG__
#pragma interface "apt-pkg/acquire-item.h"
#endif 

// Item to acquire
class pkgAcquire::Item
{   
   protected:
   bool RecheckFile(string path, string MD5, unsigned long Size);
  
   // Some private helper methods for registering URIs
   pkgAcquire *Owner;
   inline void QueueURI(ItemDesc &Item)
                 {Owner->Enqueue(Item);};
   inline void Dequeue() {Owner->Dequeue(this);};
   
   // Safe rename function with timestamp preservation
   void Rename(string From,string To);
   
   public:

   // State of the item
   enum {StatIdle, StatFetching, StatDone, StatError, StatAuthError} Status;
   string ErrorText;
   unsigned long FileSize;
   unsigned long PartialSize;   
   char *Mode;
   unsigned long ID;
   bool Complete;
   bool Local;

   // Number of queues we are inserted into
   unsigned int QueueCounter;
   
   // File to write the fetch into
   string DestFile;

   // Action members invoked by the worker
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf);
   virtual void Done(string Message,unsigned long Size,string Md5Hash,
		     pkgAcquire::MethodConfig *Cnf);
   virtual void Start(string Message,unsigned long Size);
   virtual string Custom600Headers() {return string();};
   virtual string DescURI() = 0;
   virtual void Finished() {};
   
   // Inquire functions
   virtual string MD5Sum() {return string();};
   pkgAcquire *GetOwner() {return Owner;};
   
   Item(pkgAcquire *Owner);
   virtual ~Item();
};

// Item class for index files
class pkgAcqIndex : public pkgAcquire::Item
{
   protected:
   
   const pkgSourceList::Item *Location;
   bool Decompression;
   bool Erase;
   pkgAcquire::ItemDesc Desc;
   unsigned int Retries;
   
   public:
   
   // Specialized action members
   virtual void Done(string Message,unsigned long Size,string Md5Hash,
		     pkgAcquire::MethodConfig *Cnf);
   virtual string Custom600Headers();
   virtual string DescURI() {return Location->PackagesURI();};

   pkgAcqIndex(pkgAcquire *Owner,const pkgSourceList::Item *Location);
};

// Item class for index files
class pkgAcqIndexRel : public pkgAcquire::Item
{
   protected:
   
   const pkgSourceList::Item *Location;
   pkgAcquire::ItemDesc Desc;
   unsigned int Retries;
   
   public:
   
   // Specialized action members
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf);
   virtual void Done(string Message,unsigned long Size,string Md5Hash,
		     pkgAcquire::MethodConfig *Cnf);   
   virtual string Custom600Headers();
   virtual string DescURI() {return Location->ReleaseURI();};
   
   pkgAcqIndexRel(pkgAcquire *Owner,const pkgSourceList::Item *Location);
};

// Item class for archive files
class pkgAcqArchive : public pkgAcquire::Item
{
   protected:
   
   // State information for the retry mechanism
   pkgCache::VerIterator Version;
   pkgAcquire::ItemDesc Desc;
   pkgSourceList *Sources;
   pkgRecords *Recs;
   string MD5;
   string &StoreFilename;
   pkgCache::VerFileIterator Vf;
   unsigned int Retries;

   // Queue the next available file for download.
   bool QueueNext();
   
   public:
   
   // Specialized action members
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf);
   virtual void Done(string Message,unsigned long Size,string Md5Hash,
		     pkgAcquire::MethodConfig *Cnf);
   virtual string MD5Sum() {return MD5;};
   virtual string DescURI() {return Desc.URI;};
   virtual void Finished();
   
   pkgAcqArchive(pkgAcquire *Owner,pkgSourceList *Sources,
		 pkgRecords *Recs,pkgCache::VerIterator const &Version,
		 string &StoreFilename);
};


// Item class for index files
class pkgAcqHashes : public pkgAcquire::Item
{
   protected:
   
   bool Authentication;
   pkgSourceList::RepositoryItem *Location;
   pkgAcquire::ItemDesc Desc;
   unsigned int Retries;
      
   public:
   
   // Specialized action members
   virtual void Done(string Message,unsigned long Size,string Md5Hash,
		     pkgAcquire::MethodConfig *Cnf);   
   virtual string DescURI() {return Location->HashesURI();};
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf);
   
   pkgAcqHashes(pkgAcquire *Owner,
		pkgSourceList::RepositoryItem *Location);
};

// Fetch a generic file to the current directory
class pkgAcqFile : public pkgAcquire::Item
{
   pkgAcquire::ItemDesc Desc;
   string Md5Hash;
   unsigned int Retries;
   
   public:
   
   // Specialized action members
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf);
   virtual void Done(string Message,unsigned long Size,string Md5Hash,
		     pkgAcquire::MethodConfig *Cnf);
   virtual string MD5Sum() {return Md5Hash;};
   virtual string DescURI() {return Desc.URI;};
   
   pkgAcqFile(pkgAcquire *Owner,string URI,string MD5,unsigned long Size,
	      string Desc,string ShortDesc);
};

#endif
