
/* ######################################################################

   Index Copying - Aid for copying and verifying the index files

   #####################################################################
 */

#ifndef RPMINDEXCOPY_H
#define RPMINDEXCOPY_H

#include <vector>
#include <string>

class pkgTagSection;
class FileFd;

class RPMIndexCopy
{
   protected:

   string RipComponent(const string &Path);
   string RipDirectory(const string &Path);
   string RipDistro(const string &Path);

   void ConvertToSourceList(const string &CD, string &Path);

   public:

   bool CopyPackages(const string &CDROM,const string &Name,vector<string> &List);
};


class RPMPackageCopy : public RPMIndexCopy {};

class RPMSourceCopy : public RPMIndexCopy {};

#endif
