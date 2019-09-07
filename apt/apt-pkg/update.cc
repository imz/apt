// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/acquire-item.h>
#include <apt-pkg/acquire.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/update.h>

#include <apt-pkg/luaiface.h>

#include <string>

#include <apti18n.h>
									/*}}}*/

using namespace std;

// ListUpdate - construct Fetcher and update the cache files		/*{{{*/
// ---------------------------------------------------------------------
/* This is a simple wrapper to update the cache. it will fetch stuff
 * from the network (or any other sources defined in sources.list)
 */
bool ListUpdate(pkgAcquireStatus &Stat,
                pkgSourceList &List,
                pkgCacheFile &Cache)
{
   pkgAcquire Fetcher(&Stat);

   // Lock the list directory
   FileFd Lock;
   if (!_config->FindB("Debug::NoLocking", false))
   {
      Lock.Fd(GetLock(_config->FindDir("Dir::State::Lists") + "lock"));

      if (_error->PendingError())
         return _error->Error(_("Unable to lock the list directory"));
   }

   // Run scripts
#ifdef WITH_LUA
   if (_lua->HasScripts("Scripts::AptGet::Update::Pre"))
   {
      _lua->SetDepCache(Cache);
      _lua->RunScripts("Scripts::AptGet::Update::Pre");
      _lua->ResetCaches();

      LuaCacheControl *LuaCache = _lua->GetCacheControl();
      if (LuaCache)
      {
         LuaCache->Close();
      }
   }
#endif

   pkgAcquire::RunResult res;
   bool Res = true;

   // Populate it with release file URIs
   if (!List.GetReleases(&Fetcher))
      return false;

   res = Fetcher.Run();

   bool errorsWereReported = (res == pkgAcquire::Failed);
   bool Failed = errorsWereReported;
   bool AllFailed = true;

   for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin(); 
        I != Fetcher.ItemsEnd(); ++I)
   {
      switch ((*I)->Status)
      {
      case pkgAcquire::Item::StatDone:
         AllFailed = false;
         continue;

      case pkgAcquire::Item::StatIdle:
      case pkgAcquire::Item::StatFetching:
      case pkgAcquire::Item::StatError:
         Failed = true;
         break;
      }

      (*I)->Finished();

      if (errorsWereReported)
         continue;

      ::URI uri((*I)->DescURI());
      uri.User.clear();
      uri.Password.clear();
      const std::string descUri = std::string(uri);

      _error->Warning(_("Release files for some repositories could not be retrieved or authenticated. Such repositories are being ignored."));
      _error->Error(_("Failed to fetch %s  %s"), descUri.c_str(),
                    (*I)->ErrorText.c_str());
   }

   if (errorsWereReported)
      Res = false;
   else if (Failed)
      Res = _error->Error(_("Some index files failed to download. They have been ignored, or old ones used instead."));

   // Populate it with the source selection
   if (!List.GetIndexes(&Fetcher))
      return false;

   res = Fetcher.Run();

   errorsWereReported = (res == pkgAcquire::Failed);
   if (errorsWereReported)
   {
      Failed = true;
   }

   for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin(); 
        I != Fetcher.ItemsEnd(); ++I)
   {
      switch ((*I)->Status)
      {
      case pkgAcquire::Item::StatDone:
         AllFailed = false;
         continue;

      case pkgAcquire::Item::StatIdle:
      case pkgAcquire::Item::StatFetching:
      case pkgAcquire::Item::StatError:
         Failed = true;
         break;
      }

      (*I)->Finished();

      if (errorsWereReported)
         continue;

      ::URI uri((*I)->DescURI());
      uri.User.clear();
      uri.Password.clear();
      const std::string descUri = std::string(uri);

      _error->Error(_("Failed to fetch %s  %s"), descUri.c_str(),
                    (*I)->ErrorText.c_str());
   }

   // Clean out any old list files
   // Keep "APT::Get::List-Cleanup" name for compatibility, but
   // this is really a global option for the APT library now
   if ((!Failed) &&
       (_config->FindB("APT::Get::List-Cleanup",true) &&
        _config->FindB("APT::List-Cleanup",true)))
   {
      if ((!Fetcher.Clean(_config->FindDir("Dir::State::lists")))
          || (!Fetcher.Clean(_config->FindDir("Dir::State::lists") + "partial/")))
      {
         // something went wrong with the clean
         return false;
      }
   }

   if (errorsWereReported)
      Res = false;
   else if (Failed)
      Res = _error->Error(_("Some index files failed to download. They have been ignored, or old ones used instead."));

#ifdef WITH_LUA
   // Run the success scripts if all was fine
   _lua->SetDepCache(Cache);

   if (!AllFailed)
      _lua->RunScripts("Scripts::AptGet::Update::Post-Invoke-Success");

   // Run the other scripts
   _lua->RunScripts("Scripts::AptGet::Update::Post");

   _lua->ResetCaches();
#endif

   return Res;
}
									/*}}}*/
