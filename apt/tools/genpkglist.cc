/*
 * $Id: genpkglist.cc,v 1.7 2003/01/30 17:18:21 niemeyer Exp $
 */
#include <config.h>

#include <alloca.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <rpm/rpmlib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <locale.h>
#include <stdint.h>

#include <map>
#include <iostream>

#include <tr1/unordered_set>

#include <apt-pkg/error.h>
#include <apt-pkg/tagfile.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/rpmhandler.h>

#include "cached_md5.h"

#if RPM_VERSION >= 0x040100
#include <rpm/rpmts.h>
#endif
#include <rpm/rpmio.h>

#define CRPMTAG_TIMESTAMP   1012345

int tags[] =  {
       RPMTAG_NAME, 
       RPMTAG_EPOCH,
       RPMTAG_VERSION,
       RPMTAG_RELEASE,
       RPMTAG_DISTTAG,
       RPMTAG_BUILDTIME,
       RPMTAG_GROUP,
       RPMTAG_ARCH,
       RPMTAG_PACKAGER,
       RPMTAG_SOURCERPM,
       RPMTAG_SIZE,
       RPMTAG_VENDOR,
       RPMTAG_OS,
       
       RPMTAG_DESCRIPTION, 
       RPMTAG_SUMMARY, 
       /*RPMTAG_HEADERI18NTABLE*/ HEADER_I18NTABLE,
       
       RPMTAG_REQUIREFLAGS, 
       RPMTAG_REQUIRENAME,
       RPMTAG_REQUIREVERSION,
       
       RPMTAG_CONFLICTFLAGS,
       RPMTAG_CONFLICTNAME,
       RPMTAG_CONFLICTVERSION,
       
       RPMTAG_PROVIDENAME,
       RPMTAG_PROVIDEFLAGS,
       RPMTAG_PROVIDEVERSION,
       
       RPMTAG_OBSOLETENAME,
       RPMTAG_OBSOLETEFLAGS,
       RPMTAG_OBSOLETEVERSION,

};
int numTags = sizeof(tags) / sizeof(int);



typedef struct {
   string importance;
   string date;
   string summary;
   string url;
} UpdateInfo;

// path-like Requires
static
std::tr1::unordered_set<std::string> reqfiles;

static
void addRequiredPath(const char *str)
{
   reqfiles.insert(str);
}

static
bool isRequiredPath(const char *dir, const char *basename)
{
   char fullname[strlen(dir) + strlen(basename) + 1];
   strcpy(fullname, dir);
   strcat(fullname, basename);
   if (reqfiles.find(fullname) != reqfiles.end())
      return true;
   return false;
}

// atoms are constant strings with fixed address
static
std::tr1::unordered_set<std::string> atoms;

static
const char *atom(const char *str)
{
   return atoms.insert(str).first->c_str();
}

// memory-efficient way to map path -> packages:
// dir -> <basename -> pkg+>
typedef std::multimap<const char * /* basename */, const char * /* pkg */> BPM;
static
std::map<const char * /* dir */, BPM> pathOwners;

typedef BPM::const_iterator BPI;

static
void addPathOwner(const char *dir, const char *basename, const char *pkg)
{
   BPM& bp = pathOwners[atom(dir)];
   basename = atom(basename);
   pkg = atom(pkg);
   // check if pkg is already there
   std::pair<BPI, BPI> ii = bp.equal_range(basename);
   for (BPI i = ii.first; i != ii.second; ++i) {
      if (i->second == pkg) {
         //fprintf(stderr, "already: %s%s %s\n", dir, basename, pkg);
         return;
      }
   }
   bp.insert(std::make_pair(basename, pkg));
}

static
int countPathOwners(const char *dir, const char *basename)
{
   BPM& bp = pathOwners[atom(dir)];
   basename = atom(basename);
   std::pair<BPI, BPI> ii = bp.equal_range(basename);
   return std::distance(ii.first, ii.second);
}

static
int usefulFile(const char *dir, const char *basename)
{
   // standard dirs
   if (strstr(dir, "/bin/"))
      return 1;
   if (strstr(dir, "/sbin/"))
      return 1;
   if (strstr(dir, "/etc/"))
      return 1;
   
   // libraries
   const char *pos = strstr(basename, ".so");
   if (pos > basename) {
      int c = pos[3];
      if (c == '.' || c == '\0')
         return 1;
   }

   // if this path is required by any package, it is useful
   if (isRequiredPath(dir, basename))
      return 2;

   // if the path is owned by two or more packages, it is still useful
   if (countPathOwners(dir, basename) > 1)
      return 3;

   // other paths are not useful
   return 0;
}


static
void copyStrippedFileList(Header h1, Header h2)
{
   rpm_tagtype_t bnt = 0, dnt = 0, dit = 0;
   struct {
      const char **bn, **dn;
      int32_t *di;
      rpm_count_t bnc, dnc, dic;
   } l1 = {0}, l2 = {0};

   if (!headerGetEntry(h1, RPMTAG_BASENAMES, &bnt, (void**)&l1.bn, &l1.bnc))
      return;
   if (!headerGetEntry(h1, RPMTAG_DIRNAMES, &dnt, (void**)&l1.dn, &l1.dnc)) {
      headerFreeData(l1.bn, (rpmTagType)bnt);
      return;
   }
   if (!headerGetEntry(h1, RPMTAG_DIRINDEXES, &dit, (void**)&l1.di, &l1.dic)) {
      headerFreeData(l1.bn, (rpmTagType)bnt);
      headerFreeData(l1.dn, (rpmTagType)dnt);
      return;
   }

   assert(l1.bnc == l1.dic);

   for (int i = 0; i < l1.bnc; i++) {
      const char *d = l1.dn[l1.di[i]], *b = l1.bn[i];
      int ok = usefulFile(d, b);
      // if (ok > 1) cerr << "useful(" << ok << "): " << d << b << std::endl;
      if (ok < 1)
         continue;

      if (!l2.bn) {
         l2.bn = new const char*[l1.bnc];
         l2.dn = new const char*[l1.dnc];
         l2.di = new int32_t[l1.dic];
      }

      l2.bn[l2.bnc++] = b;

      bool has_dir = false;
      for (int j = 0; j < l2.dnc; j++) {
         if (l2.dn[j] == d) {
            l2.di[l2.dic++] = j;
            has_dir = true;
            break;
         }
      }
      if (!has_dir) {
         l2.dn[l2.dnc] = d;
         l2.di[l2.dic++] = l2.dnc++;
      }
   }

   assert(l2.bnc == l2.dic);

   if (l2.bnc > 0) {
      headerAddEntry(h2, RPMTAG_BASENAMES, bnt, l2.bn, l2.bnc);
      headerAddEntry(h2, RPMTAG_DIRNAMES, dnt, l2.dn, l2.dnc);
      headerAddEntry(h2, RPMTAG_DIRINDEXES, dit, l2.di, l2.dic);
   }

   if (l2.bn) {
      delete[] l2.bn;
      delete[] l2.dn;
      delete[] l2.di;
   }

   headerFreeData(l1.bn, (rpmTagType)bnt);
   headerFreeData(l1.dn, (rpmTagType)dnt);
   headerFreeData(l1.di, (rpmTagType)dit);
}





bool loadUpdateInfo(char *path, map<string,UpdateInfo> &map)
{
   FileFd F(path, FileFd::ReadOnly);
   if (_error->PendingError()) 
   {
      return false;
   }
   
   pkgTagFile Tags(&F);
   pkgTagSection Section;
   
   while (Tags.Step(Section)) 
   {
      string file = Section.FindS("File");
      UpdateInfo info;

      info.importance = Section.FindS("Importance");
      info.date = Section.FindS("Date");
      info.summary = Section.FindS("Summary");
      info.url = Section.FindS("URL");

      map[file] = info;
   }
   return true;
}

#if RPM_VERSION >= 0x040000
// No prototype from rpm after 4.0.
extern "C" {
int headerGetRawEntry(Header h, int32_t tag, int32_t * type,
		      void *p, int32_t *c);
}
#endif

bool copyFields(Header h, Header newHeader,
		FILE *idxfile, const char *directory, char *filename,
		unsigned filesize, map<string,UpdateInfo> &updateInfo,
		bool fullFileList)
{
   int i;
   int32_t size[1];

   size[0] = filesize;
   
   // the std tags
   for (i = 0; i < numTags; i++) {
      int32_t type, count;
      void *data;
      int res;
      
      // Copy raw entry, so that internationalized strings
      // will get copied correctly.
      res = headerGetRawEntry(h, tags[i], &type, &data, &count);
      if (res != 1)
	 continue;
      headerAddEntry(newHeader, tags[i], type, data, count);
   }
 
   if (fullFileList) {
      rpm_tagtype_t type1, type2, type3;
      rpm_count_t count1, count2, count3;
      char **dnames, **bnames, **dindexes;
      int res;
   
      res = headerGetEntry(h, RPMTAG_DIRNAMES, &type1, 
			   (void**)&dnames, &count1);
      res = headerGetEntry(h, RPMTAG_BASENAMES, &type2, 
			   (void**)&bnames, &count2);
      res = headerGetEntry(h, RPMTAG_DIRINDEXES, &type3, 
			   (void**)&dindexes, &count3);

      if (res == 1) {
	 headerAddEntry(newHeader, RPMTAG_DIRNAMES, type1, dnames, count1);
	 headerAddEntry(newHeader, RPMTAG_BASENAMES, type2, bnames, count2);
	 headerAddEntry(newHeader, RPMTAG_DIRINDEXES, type3, dindexes, count3);
      }
   } else {
       copyStrippedFileList(h, newHeader);
   }
   
   // update index of srpms
   if (idxfile) {
      rpm_tagtype_t type;
      rpm_count_t count;
      char *srpm;
      char *name;
      int res;
      
      res = headerGetEntry(h, RPMTAG_NAME, &type, 
			   (void**)&name, &count);
      res = headerGetEntry(h, RPMTAG_SOURCERPM, &type, 
			   (void**)&srpm, &count);
      if (res == 1) {
	 fprintf(idxfile, "%s %s\n", srpm, name);
      }
   }
   // our additional tags
   headerAddEntry(newHeader, CRPMTAG_DIRECTORY, RPM_STRING_TYPE,
		  directory, 1);
   headerAddEntry(newHeader, CRPMTAG_FILENAME, RPM_STRING_TYPE, 
		  filename, 1);
   headerAddEntry(newHeader, CRPMTAG_FILESIZE, RPM_INT32_TYPE,
		  size, 1);
      
   // update description tags
   if (updateInfo.find(string(filename)) != updateInfo.end()) {
      const char *tmp;
      string name = string(filename);
      
      tmp = updateInfo[name].summary.c_str();
      headerAddEntry(newHeader, CRPMTAG_UPDATE_SUMMARY,
		     RPM_STRING_TYPE,
		     tmp, 1);
      tmp = updateInfo[name].url.c_str();
      headerAddEntry(newHeader, CRPMTAG_UPDATE_URL,
		     RPM_STRING_TYPE,
		     tmp, 1);
      tmp = updateInfo[name].date.c_str();
      headerAddEntry(newHeader, CRPMTAG_UPDATE_DATE,
		     RPM_STRING_TYPE,
		     tmp, 1);
      tmp = updateInfo[name].importance.c_str();
      headerAddEntry(newHeader, CRPMTAG_UPDATE_IMPORTANCE,
		     RPM_STRING_TYPE,
		     tmp, 1);
   }
   
   return true;
}


int selectDirent(const struct dirent *ent)
{
   int state = 0;
   const char *p = ent->d_name;
   
   while (1) {
      if (*p == '.') {
	  state = 1;
      } else if (state == 1 && *p == 'r')
	  state++;
      else if (state == 2 && *p == 'p')
	  state++;
      else if (state == 3 && *p == 'm')
	  state++;
      else if (state == 4 && *p == '\0')
	  return 1;
      else if (*p == '\0')
	  return 0;
      else
	  state = 0;
      p++;
   }
}


void usage()
{
   cerr << "genpkglist " << VERSION << endl;
   cerr << "usage: genpkglist [<options>] <dir> <suffix>" << endl;
   cerr << "options:" << endl;
   cerr << " --index <file>  file to write srpm index data to" << endl;
   cerr << " --info <file>   file to read update info from" << endl;
   cerr << " --meta <suffix> create package file list with given suffix" << endl;
   cerr << " --bloat         do not strip the package file list. Needed for some" << endl;
   cerr << "                 distributions that use non-automatically generated" << endl;
   cerr << "                 file dependencies" << endl;
   cerr << " --append        append to the package file list, don't overwrite" << endl;
   cerr << " --progress      show a progress bar" << endl;
   cerr << " --cachedir=DIR  use a custom directory for package md5sum cache"<<endl;
}



#ifndef HAVE_SCANDIR
// from glibc 1.09.1  mod'd by jmik, ins'd by asm, fix'd by sbi
int alphasort(const void * a, const void * b)
{
  return strcmp ((*(struct dirent **) a)->d_name,
                 (*(struct dirent **) b)->d_name);
}

int scandir(const char * dir, struct dirent *** namelist, 
        int (* select)(struct dirent *), 
        int (* cmp)(const void *, const void *))

{
  DIR *dp = opendir (dir);
  struct dirent **v = NULL;
  size_t vsize = 0, i;
  struct dirent *d;
  int save;

  if (dp == NULL)
    return -1;

  save = errno;
  errno = 0;

  i = 0;
  while ((d = readdir (dp)) != NULL)
    {
    if (select == NULL || (*select) (d))
      {
        if (i == vsize)
          {
            struct dirent **newv;
            if (vsize == 0)
              vsize = 10;
            else
              vsize *= 2;
            newv = (struct dirent **) realloc (v, vsize * sizeof (*v));
            if (newv == NULL)
              {
              lose:
                errno = ENOMEM;
                break;
              }
            v = newv;
          }

        v[i] = (struct dirent *) malloc (d->d_reclen);
        if (v[i] == NULL)
          goto lose;

        // *v[i++] = *d;
	memcpy(v[i], d, d->d_reclen);
	i++;
      }
    }

  if (errno != 0)
    {
      save = errno;
      (void) closedir (dp);
      while (i > 0)
        free (v[--i]);
      free (v);
      errno = save;
      return -1;
    }

  (void) closedir (dp);
  errno = save;

  if (v != NULL)
  {
    v[i] = NULL;
    /* Sort the list if we have a comparison function to sort with.  */
    if (cmp != NULL)
      qsort (v, i, sizeof (struct dirent *), cmp);
  }

  *namelist = v;
  return i;
}
// end of new stuff from glibc
#endif /* !HAVE_SCANDIR */


int main(int argc, char ** argv) 
{
   string rpmsdir;
   string pkglist_path;
   FD_t outfd, fd;
   struct dirent **dirEntries;
   int entry_no, entry_cur;
   map<string,UpdateInfo> updateInfo;
   CachedMD5 *md5cache;
   char *op_dir;
   char *op_suf;
   char *op_index = NULL;
   char *op_update = NULL;
   FILE *idxfile;
   int i;
   bool fullFileList = false;
   bool progressBar = false;
   const char *pkgListSuffix = NULL;
   bool pkgListAppend = false;
   
   setlocale(LC_ALL, "C");
   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--index") == 0) {
	 i++;
	 if (i < argc) {
	    op_index = argv[i];
	 } else {
	    cout << "genpkglist: filename missing for option --index"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--info") == 0) {
	 i++;
	 if (i < argc) {
	    op_update = argv[i];
	 } else {
	    cout << "genpkglist: filename missing for option --info"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--bloat") == 0) {
	 fullFileList = true;
      } else if (strcmp(argv[i], "--progress") == 0) {
	 progressBar = true;
      } else if (strcmp(argv[i], "--append") == 0) {
	 pkgListAppend = true;
      } else if (strcmp(argv[i], "--meta") == 0) {
	 i++;
	 if (i < argc) {
	    pkgListSuffix = argv[i];
	 } else {
	    cout << "genpkglist: argument missing for option --meta"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--cachedir") == 0) {
	 i++;
	 if (i < argc) {
            _config->Set("Dir::Cache", argv[i]);
	 } else {
            cout << "genpkglist: argument missing for option --cachedir"<<endl;
	    exit(1);
	 }
      } else {
	 break;
      }
   }
   if (argc - i > 0)
       op_dir = argv[i++];
   else {
      usage();
      exit(1);
   }
   if (argc - i > 0)
       op_suf = argv[i++];
   else {
      usage();
      exit(1);
   }
   if (argc != i) {
      usage();
   }
   
   if (op_update) {
      if (!loadUpdateInfo(op_update, updateInfo)) {
	 cerr << "genpkglist: error reading update info from file " << op_update << endl;
	 _error->DumpErrors();
	 exit(1);
      }
   }
   if (op_index) {
      idxfile = fopen(op_index, "w+");
      if (!idxfile) {
	 cerr << "genpkglist: could not open " << op_index << " for writing";
	 perror("");
	 exit(1);
      }
   } else {
      idxfile = NULL;
   }
   
   {
      char cwd[200];
      
      getcwd(cwd, 200);
      if (*op_dir != '/') {
	 rpmsdir = string(cwd) + "/" + string(op_dir);
      } else {
	 rpmsdir = string(op_dir);
      }
   }
   pkglist_path = string(rpmsdir);
   rpmsdir = rpmsdir + "/RPMS." + string(op_suf);

   string dirtag = "RPMS." + string(op_suf);

   entry_no = scandir(rpmsdir.c_str(), &dirEntries, selectDirent, alphasort);
   if (entry_no < 0) {
      cerr << "genpkglist: error opening directory " << rpmsdir << ":"
	  << strerror(errno);
      return 1;
   }
   
   chdir(rpmsdir.c_str());
   
   if (pkgListSuffix != NULL)
	   pkglist_path = pkglist_path + "/base/pkglist." + pkgListSuffix;
   else
	   pkglist_path = pkglist_path + "/base/pkglist." + op_suf;
   
   
   if (pkgListAppend == true && FileExists(pkglist_path)) {
      outfd = Fopen(pkglist_path.c_str(), "a");
   } else {
      unlink(pkglist_path.c_str());
      outfd = Fopen(pkglist_path.c_str(), "w");
   }
   if (!outfd) {
      cerr << "genpkglist: error creating file" << pkglist_path << ":"
	  << strerror(errno);
      return 1;
   }

   md5cache = new CachedMD5(string(op_dir) + string(op_suf), "genpkglist");

#if RPM_VERSION >= 0x040100
   rpmReadConfigFiles(NULL, NULL);
   rpmts ts = rpmtsCreate();
   rpmtsSetVSFlags(ts, (rpmVSFlags_e)-1);
#else
   int isSource;
#endif   

   if (!fullFileList) {
      // ALT: file list cannot be stripped in a dumb manner -- this is going
      // to produce unmet dependencies.  First pass is required to initialize
      // certain data structures.
      for (entry_cur = 0; entry_cur < entry_no; entry_cur++) {
         if (progressBar) {
            if (entry_cur)
               printf("\b\b\b\b\b\b\b\b\b\b");
            printf(" %04i/%04i", entry_cur + 1, entry_no);
            fflush(stdout);
         }

         fd = Fopen(dirEntries[entry_cur]->d_name, "r");
         if (!fd)
            continue;
         int rc;
         Header h;
#if RPM_VERSION >= 0x040100
         rc = rpmReadPackageFile(ts, fd, dirEntries[entry_cur]->d_name, &h);
         if (rc == RPMRC_OK || rc == RPMRC_NOTTRUSTED || rc == RPMRC_NOKEY) {
#else
         rc = rpmReadPackageHeader(fd, &h, &isSource, NULL, NULL);
         if (rc == 0) {
#endif
            // path-like Requires
            rpm_tagtype_t reqtype = 0;
            rpm_count_t nreq = 0;
            const char **requires = NULL;
            rc = headerGetEntry(h, RPMTAG_REQUIRENAME, &reqtype, (void**)&requires, &nreq);
            if (rc == 1) {
               if (reqtype == RPM_STRING_ARRAY_TYPE) {
                  int i;
                  for (i = 0; i < nreq; i++) {
                     const char *req = requires[i];
                     if (*req == '/') {
                        // cerr << dirEntries[entry_cur]->d_name << " requires " << req << endl;
                        addRequiredPath(req);
                     }
                  }
               }
            }
            headerFreeTag(h, requires, (rpmTagType)reqtype);

            // path ownership
            char *pkg = NULL;
            rpm_tagtype_t pkgt = 0;
            const char **bn = NULL, **dn = NULL;
            int32_t *di = NULL;
            rpm_tagtype_t bnt = 0, dnt = 0, dit = 0;
            rpm_count_t bnc = 0;
            rc = headerGetEntry(h, RPMTAG_NAME, &pkgt, (void**)&pkg, NULL)
              && headerGetEntry(h, RPMTAG_BASENAMES, &bnt, (void**)&bn, &bnc)
              && headerGetEntry(h, RPMTAG_DIRNAMES, &dnt, (void**)&dn, NULL)
              && headerGetEntry(h, RPMTAG_DIRINDEXES, &dit, (void**)&di, NULL)
              ;
            if (rc == 1) {
               int i;
               for (i = 0; i < bnc; i++)
                  addPathOwner(dn[di[i]], bn[i], pkg);
            }
            headerFreeTag(h, pkg, pkgt);
            headerFreeTag(h, bn, bnt);
            headerFreeTag(h, dn, dnt);
            headerFreeTag(h, di, dit);
            
            headerFree(h);
         }
         Fclose(fd);
      }
   }
   for (entry_cur = 0; entry_cur < entry_no; entry_cur++) {
      struct stat sb;

      if (progressBar) {
         if (entry_cur)
            printf("\b\b\b\b\b\b\b\b\b\b");
         printf(" %04i/%04i", entry_cur + 1, entry_no);
         fflush(stdout);
      }

      if (stat(dirEntries[entry_cur]->d_name, &sb) < 0) {
	    cerr << "\nWarning: " << strerror(errno) << ": " << 
		    dirEntries[entry_cur]->d_name << endl;
	    continue;
      }

      {
	 Header h;
	 int rc;
	 
	 fd = Fopen(dirEntries[entry_cur]->d_name, "r");

	 if (!fd) {
	    cerr << "\nWarning: " << strerror(errno) << ": " << 
		    dirEntries[entry_cur]->d_name << endl;
	    continue;
	 }
	 
#if RPM_VERSION >= 0x040100
	 rc = rpmReadPackageFile(ts, fd, dirEntries[entry_cur]->d_name, &h);
	 if (rc == RPMRC_OK || rc == RPMRC_NOTTRUSTED || rc == RPMRC_NOKEY) {
#else
	 rc = rpmReadPackageHeader(fd, &h, &isSource, NULL, NULL);
	 if (rc == 0) {
#endif
	    Header newHeader;
	    char md5[34];
	    
	    newHeader = headerNew();
	    
	    copyFields(h, newHeader, idxfile, dirtag.c_str(),
		       dirEntries[entry_cur]->d_name,
		       sb.st_size, updateInfo, fullFileList);

	    md5cache->MD5ForFile(string(dirEntries[entry_cur]->d_name), 
				 sb.st_mtime, md5);
	    headerAddEntry(newHeader, CRPMTAG_MD5, RPM_STRING_TYPE, md5, 1);

	    headerWrite(outfd, newHeader, HEADER_MAGIC_YES);
	    
	    headerFree(newHeader);
	    headerFree(h);
	 } else {
	    cerr << "\nWarning: Skipping malformed RPM: " << 
		    dirEntries[entry_cur]->d_name << endl;
	 }
	 Fclose(fd);
      }
   }

   Fclose(outfd);

#if RPM_VERSION >= 0x040100
   ts = rpmtsFree(ts);
#endif
   
   delete md5cache;

   return 0;
}
