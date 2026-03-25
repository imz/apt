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
#include <algorithm>

#include <apti18n.h>
#include <stdlib.h>
#include <sys/wait.h>
									/*}}}*/

using namespace std;

static bool checkSignature(pkgAcquire::Item* data_f,
                           pkgAcquire::Item* sig_f)
{
   int fd[2];
   if (pipe(fd) < 0) {
      _error->Warning("Could not create a pipe for signature verificator.");
      return false;
   }

   pid_t pid = fork();
   if (pid < 0) {
      _error->Warning("Could not spawn signature verificator.");
      return false;
   } else if (pid == 0) {
      close(fd[0]);
      close(STDERR_FILENO);
      close(STDOUT_FILENO);
      dup2(fd[1], STDOUT_FILENO);
      dup2(fd[1], STDERR_FILENO);

      unsetenv("LANG");
      unsetenv("LANGUAGE");
      unsetenv("LC_ALL");
      unsetenv("LC_MESSAGES");
      unsetenv("LC_CTYPE");

      string path = "/usr/lib/apt/verify_sig";
      string homedir = "";
      const char *argv[4];

      argv[0] = "verify_sig";
      argv[1] = data_f->DestFile.c_str();
      argv[2] = sig_f->DestFile.c_str();
      argv[3] = NULL;

      execvp(path.c_str(), (char**) argv);
      exit(111);
   }

   close(fd[1]);
   FILE* f = fdopen(fd[0], "r");

   char buf[1024];
   while (true) {
      if (fgets(buf, sizeof(buf), f)) {
         _error->Warning("%s", buf);
      } else {
         break;
      }
   }
   fclose(f);

   int status;
   waitpid(pid, &status, 0);

   if (WEXITSTATUS(status) == 111)
      _error->Warning("Unable to execute the signature verificator.");

   return WEXITSTATUS(status) == 0;
}

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
   if (! (List.InvalidateReleases() && List.GetReleases(&Fetcher)) )
      return false;

   res = Fetcher.Run();

   bool errorsWereReported = (res == pkgAcquire::Failed);
   bool Failed = errorsWereReported;
   bool AllFailed = true;

   for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin();
        I != Fetcher.ItemsEnd(); ++I)
   {
      ::URI uri((*I)->DescURI());
      uri.User.clear();
      uri.Password.clear();
      const string descUri = string(uri);

      switch ((*I)->Status)
      {
      case pkgAcquire::Item::StatDone:
         AllFailed = false;
         if (descUri.compare(descUri.length() - 4, 4, ".sig") == 0) {
            auto data_f = find_if(Fetcher.ItemsBegin(), Fetcher.ItemsEnd(), [&](pkgAcquire::Item* J) -> bool {
               ::URI j_uri(J->DescURI());
               j_uri.User.clear();
               j_uri.Password.clear();
               const string j_uri_str = string(j_uri);
               return J->Complete && (descUri.compare(0, descUri.length() - 4, j_uri_str) == 0);
            });
            if (data_f != Fetcher.ItemsEnd()) {
               if (!checkSignature(*data_f, *I)) {
                  Failed = true;
                  _error->Error("Signature verification falied.");
                  errorsWereReported = true;
                  break;
               }
            } else {
               Failed = true;
               _error->Error("Signed file %s wasn't fetched!",
                             descUri.substr(0, descUri.length() - 4).c_str());
               errorsWereReported = true;
               break;
            }
         }
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

   for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin();
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
       (_config->FindB("APT::Get::List-Cleanup", true) &&
        _config->FindB("APT::List-Cleanup", true)))
   {
      if (!Fetcher.Clean(_config->FindDir("Dir::State::lists"))
          || !Fetcher.Clean(_config->FindDir("Dir::State::lists") + "partial/"))
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
