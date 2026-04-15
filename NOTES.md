<!-- -*- mode: markdown; -*- -->

Прошу прощения, что сразу не написал, как посмотрел; думал, ещё поглубже подумать.

Предстоит посмотреть и прокомментировать код, добавляющий подпись нового типа в apt.

git://git.altlinux.org/people/manowar/packages/apt.git ветка master

Подпись нового типа кладётся и скачивается в отдельном файле.

## О свойствах реализаций проверки подписи через внешний инструмент

Сейчас все "методы" -- скомпилированные бинарники. А предлагается скрипт (с вызовом openssl для нового типа подписи).

/usr/lib64/apt/methods/gpg и пр.

Это же, наверно, ради некоторых лучших свойств, которых у скриптов нет?..

## Проверка подписи в логике работы apt

Общие соображения о том, чего бы я ожидал (и о которых хочется подумать, перепроверить):

1. проверка подписи нового типа должна происходить в тех же местах кода, что и проверка уже имеющегося типа подписи; с той же обязательностью успешности проверки или отказом работать.
2. обязательность успешной проверки в случае, если мы ожидаем подписи.

На счёт 2. вообще я ожидаю, что если в sources.list указано, каким ключом подписано (в квадратных скобках), то проверка обязательно должна быть успешна, иначе отказ работать. Для подписи нового типа я бы ожидал такого же поведения: указание в sources.list -- обязательное требование. Как это можно совместить с двумя типами подписи? (Ну, например, под одними именем два публичных ключа разного типа.)

## Комментарии к коду (коммитам)

Ещё раз пересмотрю. Если будет, что-то кроме глобальных вопросов выше, напишу. Пока только мелкое замечание про std::.

> commit ce0e0aaaa24eed6755a49dfc7517cd61dfe21870
> Author: Paul Wolneykien <manowar@altlinux.org>
> Date:   Tue Mar 24 22:19:28 2026 +0300
> 
>     Download release.sig file if APT::Get::OpenSSL option is set
> 
> diff --git a/apt-pkg/rpm/rpmindexfile.cc b/apt-pkg/rpm/rpmindexfile.cc
> index 95e99f3da..2eb145710 100644
> --- a/apt-pkg/rpm/rpmindexfile.cc
> +++ b/apt-pkg/rpm/rpmindexfile.cc
> @@ -109,6 +109,10 @@ bool rpmListIndex::GetReleases(pkgAcquire *Owner) const
>     Repository->Acquire = false;
>     new pkgAcqIndexRel(Owner,Repository,ReleaseURI("release"),
>  		      ReleaseInfo("release"), "release", true);
> +
> +   if (_config->FindB("APT::Get::OpenSSL", false) == true)
> +      new pkgAcqIndexRel(Owner,Repository,ReleaseURI("release.sig"),
> +                         ReleaseInfo("release.sig"), "release.sig", true);
>     return true;
>  }
>  									/*}}}*/
> 
> commit 17d1d90fb41535cbbbbd553dc3d0a5b07f1c08ab
> Author: Paul Wolneykien <manowar@altlinux.org>
> Date:   Wed Mar 25 13:37:06 2026 +0300
> 
>     Draft: Invoke /usr/lib/apt/verify_sig to verify *.sig files
> 
> diff --git a/apt-pkg/acquire-item.cc b/apt-pkg/acquire-item.cc
> index 906eb287d..dec069ae3 100644
> --- a/apt-pkg/acquire-item.cc
> +++ b/apt-pkg/acquire-item.cc
> @@ -635,6 +635,9 @@ void pkgAcqIndexRel::DoneByWorker(const string &Message,
>     }
>     else
>     {
> +      if (DestFile.compare(DestFile.length() - 4, 4, ".sig") == 0)
> +         return;
> +
>        if (FileName == DestFile)
>  	 Erase = true;
>        else
> diff --git a/apt-pkg/update.cc b/apt-pkg/update.cc
> index 12672cba6..93e56102d 100644
> --- a/apt-pkg/update.cc
> +++ b/apt-pkg/update.cc
> @@ -14,12 +14,76 @@
>  #include <apt-pkg/luaiface.h>
>  
>  #include <string>
> +#include <algorithm>
>  
>  #include <apti18n.h>
> +#include <stdlib.h>
> +#include <sys/wait.h>
>  									/*}}}*/
>  
>  using namespace std;
>  
> +static bool checkSignature(pkgAcquire::Item* data_f,
> +                           pkgAcquire::Item* sig_f)
> +{
> +   int fd[2];
> +   if (pipe(fd) < 0) {
> +      _error->Warning("Could not create a pipe for signature verificator.");
> +      return false;
> +   }
> +
> +   pid_t pid = fork();
> +   if (pid < 0) {
> +      _error->Warning("Could not spawn signature verificator.");
> +      return false;
> +   } else if (pid == 0) {
> +      close(fd[0]);
> +      close(STDERR_FILENO);
> +      close(STDOUT_FILENO);
> +      dup2(fd[1], STDOUT_FILENO);
> +      dup2(fd[1], STDERR_FILENO);
> +
> +      unsetenv("LANG");
> +      unsetenv("LANGUAGE");
> +      unsetenv("LC_ALL");
> +      unsetenv("LC_MESSAGES");
> +      unsetenv("LC_CTYPE");
> +
> +      string path = "/usr/lib/apt/verify_sig";
> +      string homedir = "";
> +      const char *argv[4];
> +
> +      argv[0] = "verify_sig";
> +      argv[1] = data_f->DestFile.c_str();
> +      argv[2] = sig_f->DestFile.c_str();
> +      argv[3] = NULL;
> +
> +      execvp(path.c_str(), (char**) argv);
> +      exit(111);
> +   }
> +
> +   close(fd[1]);
> +   FILE* f = fdopen(fd[0], "r");
> +
> +   char buf[1024];
> +   while (true) {
> +      if (fgets(buf, sizeof(buf), f)) {
> +         _error->Warning("%s", buf);
> +      } else {
> +         break;
> +      }
> +   }
> +   fclose(f);
> +
> +   int status;
> +   waitpid(pid, &status, 0);
> +
> +   if (WEXITSTATUS(status) == 111)
> +      _error->Warning("Unable to execute the signature verificator.");
> +
> +   return WEXITSTATUS(status) == 0;
> +}
> +
>  // ListUpdate - construct Fetcher and update the cache files		/*{{{*/
>  // ---------------------------------------------------------------------
>  /* This is a simple wrapper to update the cache. it will fetch stuff
> @@ -73,10 +137,38 @@ bool ListUpdate(pkgAcquireStatus &Stat,
>     for (pkgAcquire::ItemCIterator I = Fetcher.ItemsBegin();
>          I != Fetcher.ItemsEnd(); ++I)
>     {
> +      ::URI uri((*I)->DescURI());
> +      uri.User.clear();
> +      uri.Password.clear();
> +      const string descUri = string(uri);

Тут исчез префикс std:: в std::string. А в коде apt тенденция наоборот: писать явно std:: и по возможности отказываться от using namespace std.

> +
>        switch ((*I)->Status)
>        {
>        case pkgAcquire::Item::StatDone:
>           AllFailed = false;
> +         if (descUri.compare(descUri.length() - 4, 4, ".sig") == 0) {
> +            auto data_f = find_if(Fetcher.ItemsBegin(), Fetcher.ItemsEnd(), [&](pkgAcquire::Item* J) -> bool {
> +               ::URI j_uri(J->DescURI());
> +               j_uri.User.clear();
> +               j_uri.Password.clear();
> +               const string j_uri_str = string(j_uri);
> +               return J->Complete && (descUri.compare(0, descUri.length() - 4, j_uri_str) == 0);
> +            });
> +            if (data_f != Fetcher.ItemsEnd()) {
> +               if (!checkSignature(*data_f, *I)) {
> +                  Failed = true;
> +                  _error->Error("Signature verification falied.");
> +                  errorsWereReported = true;
> +                  break;
> +               }
> +            } else {
> +               Failed = true;
> +               _error->Error("Signed file %s wasn't fetched!",
> +                             descUri.substr(0, descUri.length() - 4).c_str());
> +               errorsWereReported = true;
> +               break;
> +            }
> +         }
>           continue;
>  
>        case pkgAcquire::Item::StatIdle:
> @@ -91,11 +183,6 @@ bool ListUpdate(pkgAcquireStatus &Stat,
>        if (errorsWereReported)
>           continue;
>  
> -      ::URI uri((*I)->DescURI());
> -      uri.User.clear();
> -      uri.Password.clear();
> -      const std::string descUri = std::string(uri);

Тут std:: было, а в новом коде не стало.

> -
>        _error->Warning(_("Release files for some repositories could not be retrieved or authenticated. Such repositories are being ignored."));
>        _error->Error(_("Failed to fetch %s  %s"), descUri.c_str(),
>                      (*I)->ErrorText.c_str());
> 
> commit e6391d77bc1f8d1af6c4036147e3fb7857951124
> Author: Paul Wolneykien <manowar@altlinux.org>
> Date:   Wed Mar 25 14:23:03 2026 +0300
> 
>     Added the 'verify_sig' script
> 
> diff --git a/cmdline/Makefile.am b/cmdline/Makefile.am
> index 2221fb951..b56f04421 100644
> --- a/cmdline/Makefile.am
> +++ b/cmdline/Makefile.am
> @@ -4,6 +4,7 @@ libexecaptdir=$(libexecdir)/apt
>  EXTRA_DIST = indexcopy.cc indexcopy.h
>  
>  libexecapt_PROGRAMS = apt-get
> +libexecapt_SCRIPTS = verify_sig
>  bin_PROGRAMS = apt-cache apt-cdrom apt-config apt-mark
>  
>  if COMPILE_APTSHELL
> diff --git a/cmdline/verify_sig b/cmdline/verify_sig
> new file mode 100755
> index 000000000..8ceec5db0
> --- /dev/null
> +++ b/cmdline/verify_sig
> @@ -0,0 +1,171 @@
> +#!/bin/sh -efu
> +
> +# Copyright (C) 2026 Paul Wolneykien <manowar@altlinux.org>
> +#
> +# This program is free software; you can redistribute it and/or modify
> +# it under the terms of the GNU General Public License as published by
> +# the Free Software Foundation; either version 2 of the License, or
> +# (at your option) any later version.
> +#
> +# This program is distributed in the hope that it will be useful,
> +# but WITHOUT ANY WARRANTY; without even the implied warranty of
> +# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
> +# GNU General Public License for more details.
> +#
> +# You should have received a copy of the GNU General Public License along
> +# with this program; if not, write to the Free Software Foundation, Inc.,
> +# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
> +
> +PROG=${0##*/}
> +VERSION='0.1.0'
> +YEAR=2026
> +
> +DEFUALT_CONFIG=/etc/apt/verify_sig.conf
> +
> +usage()
> +{
> +    [ "$1" = 0 ] || exec >&2
> +    cat <<EOF
> +Usage: $PROG [ options ] DATA_FILE SIG_FILE
> +
> +Options:
> +
> +  -c CONF_FILE, --config=CONF_FILE    configuration file to use (the
> +                                    default is $DEFUALT_CONFIG);
> +
> +  -C OSSL_CONF, --ossl-config=OSSL_CONF    OpenSSL configuration file
> +                                         to use;
> +
> +  -H KEY_HASH, --key-hash=KEY_HASH    hash of the X.509 public
> +     	       			    certificate of the signer;
> +
> +  -v, --verbose           be verbose;
> +
> +  -V, --version           print program version and exit;
> +
> +  -h, --help              show this text and exit.
> +
> +Report bugs to https://bugzilla.altlinux.org/.
> +EOF
> +    exit "${1:-0}"
> +}
> +
> +TEMP="$(getopt -n "$PROG" -o c:C:HvVh -l config:,ossl-config:,key-hash:,verbose,version,help -- "$@")" || usage 1
> +eval set -- "$TEMP"
> +
> +config="$DEFUALT_CONFIG"
> +ossl_config=
> +key_hash=
> +verbose=
> +while :; do
> +    case "$1" in
> +	-c|--config)
> +	    shift
> +	    config="$1"
> +	    ;;
> +	-C|--ossl-config)
> +	    shift
> +	    ossl_config="$1"
> +	    ;;
> +	-H|--key-hash)
> +	    shift
> +	    key_hash="$1"
> +	    ;;
> +	-v|--verbose)
> +	    verbose=y
> +	    ;;
> +        -h|--help)
> +	    usage 0
> +            ;;
> +	-V|--version)
> +	    cat <<EOF
> +$VERSION $YEAR
> +This program is free software; you can redistribute it and/or modify
> +it under the terms of the GNU General Public License as published by
> +the Free Software Foundation; either version 2 of the License, or
> +(at your option) any later version.
> +EOF
> +	    exit 0
> +	    ;;
> +        --)
> +	    shift
> +	    break
> +            ;;
> +        *)
> +	    echo "$PROG: unrecognized option: $1" >&2
> +	    usage 1
> +            ;;
> +    esac
> +    shift
> +done
> +
> +if [ $# -ne 2 ]; then
> +    usage 1
> +fi
> +
> +if [ -e "$config" ]; then
> +    # shellcheck disable=SC1090
> +    . "$config"
> +fi
> +
> +if [ -n "$ossl_config" ]; then
> +    OPENSSL_CONF="$ossl_config"
> +fi
> +
> +if [ -n "${OPENSSL_CONF:-}" ]; then
> +    export OPENSSL_CONF
> +fi
> +
> +data_f="$1"; shift
> +sig_f="$1"; shift
> +
> +if [ ! -e "$data_f" ]; then
> +    case "$data_f" in
> +	/var/lib/apt/lists/partial/*)
> +	    _data_f=/var/lib/apt/lists/"${data_f#/var/lib/apt/lists/partial/}"
> +	    if [ -e "$_data_f" ]; then
> +		data_f="$_data_f"
> +	    fi
> +    esac
> +fi
> +
> +if [ ! -e "$data_f" ]; then
> +    echo "Signed file $data_f not found!" >&2
> +    exit 1
> +fi
> +
> +if [ ! -e "$sig_f" ]; then
> +    echo "Signature file $sig_f not found!" >&2
> +    exit 1
> +fi
> +
> +cert=
> +if [ -n "$key_hash" ]; then
> +    #cert=...
> +    echo "Sorry, not yet implemented." >&2
> +    exit 1
> +elif [ -n "${DEFAULT_KEY:-}" ]; then
> +    cert="$DEFAULT_KEY"
> +else
> +    echo "Unable to verify the signature: No key hash specified and DEFAULT_KEY is also not configured in $config." >&2
> +    exit 1
> +fi
> +
> +if [ -n "${CA_PATH:-}${CA_FILE:-}" ]; then
> +    if ! openssl verify ${CA_PATH:+-CApath "$CA_PATH"} \
> +	                ${CA_FILE:+-CAfile "$CA_FILE"} \
> +	 "$cert"
> +    then
> +	echo "Certificate verification error." >&2
> +	exit 1
> +    fi
> +fi
> +
> +if ! openssl pkeyutl -verify -certin -inkey "$cert" -in "$data_f" -rawin -sigfile "$sig_f"
> +then
> +    echo "Signature is not valid." >&2
> +    exit 1
> +fi
> +
> +[ -z "$verbose" ] || echo "Signature is valid." >&2
> +exit 0

<!-- Local Variables: -->
<!-- ispell-local-dictionary: "russian" -->
<!-- End: -->
