# 5a4fdbf0b Fix every use of va_list: add proper cleanup

ok

# 6f5bb983d strutil.cc: rework string iterating

ok

comment: Такое изменение

    -   for (string::const_iterator I = Str.begin(); I != Str.end(); I++)
    +   for (size_t i = 0; i < Str.size(); ++i)

делает код более корректным, избавляет от undefined behavior.
 -- https://bugzilla.altlinux.org/show_bug.cgi?id=30482#c9

# 10bf66858 gpg.cc: fix potential memory leak

ok

comment: В этом консервативном изменении делать необязательно, но можно ещё в
таких случаях использовать strdupa() (что, конечно, нехорошо с
т.зр. обработки ошибок). Да и не только в консерватизме дело, а том,
что strdupa() ускоряет работу, но для функций, на которые небольшая
нагрузка, это несущественно.

# c138c4850 RPM ArchiveURI: check file length before using it

ok

# ec437de01 Use 'override' keyword

ok (более строгий код; если что, компилятор сообщит об ошибке)

Проверили, что нет других изменений с помощью:

find -type f '-(' -name '*.cc' -o -name '*.h' '-)' -print0 | xargs -0 sed -i -re 's: override( |;|$):\1:'

# c742cecda Avoid copying objects

Теоретически есть опасность, что в функциях возьмут указатель на
строку (ссылку) и через него поменяют значение снаружи или что-нибудь
в этом роде, но скорее всего так никто не делает в этом коде.

Проверяем что нет других нетривиальных изменений с помощью:

    find -type f '-(' -name '*.cc' -o -name '*.h' '-)' -print0 | xargs -0 sed -i -re 's:const ([^ ]+) &:\1 :g'
    git commit ...
    git diff @~2.. -w

Видим нетривиальные изменения:

1. https://bugzilla.altlinux.org/show_bug.cgi?id=30482#c9:
от копирования совсем избавиться не удалось

diff --git a/apt/apt-pkg/acquire-method.cc b/apt/apt-pkg/acquire-method.cc
index d72a984..b72a90c 100644
--- a/apt/apt-pkg/acquire-method.cc
+++ b/apt/apt-pkg/acquire-method.cc
@@ -93,10 +93,12 @@ void pkgAcqMethod::Fail(bool Transient)
 // AcqMethod::Fail - A fetch has failed                                        /*{{{*/
 // ---------------------------------------------------------------------
 /* */
-void pkgAcqMethod::Fail(string Err,bool Transient)
+void pkgAcqMethod::Fail(const char *Why, bool Transient)
 {
+   std::string Err = Why;
+
    // Strip out junk from the error messages
-   for (string::iterator I = Err.begin(); I != Err.end(); I++)
+   for (auto I = Err.begin(); I != Err.end(); ++I)
    {
       if (*I == '\r') 
         *I = ' ';
diff --git a/apt/apt-pkg/acquire-method.h b/apt/apt-pkg/acquire-method.h
index 42bd9d3..ac66e30 100644
--- a/apt/apt-pkg/acquire-method.h
+++ b/apt/apt-pkg/acquire-method.h
@@ -66,8 +66,8 @@ class pkgAcqMethod
    
    // Outgoing messages
    void Fail(bool Transient = false);
-   inline void Fail(const char *Why, bool Transient = false) {Fail(string(Why),Transient);};
-   void Fail(string Why, bool Transient = false);
+   inline void Fail(string Why, bool Transient = false) { Fail(Why.c_str(), Transient);};
+   void Fail(const char *Why, bool Transient = false);
    void URIStart(FetchResult &Res);
    void URIDone(FetchResult &Res,FetchResult *Alt = 0);
    bool MediaFail(string Required,string Drive);

3. тоже не подходит под commit message:

diff --git a/apt/apt-pkg/cacheiterators.h b/apt/apt-pkg/cacheiterators.h
index 3fcec06..6e69d51 100644
--- a/apt/apt-pkg/cacheiterators.h
+++ b/apt/apt-pkg/cacheiterators.h
@@ -186,7 +186,7 @@ class pkgCache::DepIterator
    inline unsigned long Index() const {return Dep - Owner->DepP;};
    // CNC:2003-02-17 - This is a very used function, so it's now
    //                 inlined here.
-   inline bool IsCritical()
+   inline bool IsCritical() const
                {
                        switch (Dep->Type) {
                                case pkgCache::Dep::Conflicts:

Отдельный коммит сделать?

4. Не подходит под commit message:

apt/apt-pkg/contrib/configuration.cc ---------------------
index a8a0e68..67d0c8a 100644
@@ -52,14 +52,14 @@ Configuration::Configuration(const Item *Root) : Root((Item *)Root), ToFree(fals
 };
 
 // CNC:2003-02-23 - Copy constructor.
-Configuration::Configuration(Configuration &Conf) : ToFree(true)
+Configuration::Configuration(const Configuration &Conf) : ToFree(true)
 {
    Root = new Item;
    if (Conf.Root->Child)
       CopyChildren(Conf.Root, Root);
 }
 
-void Configuration::CopyChildren(Item *From, Item *To)
+void Configuration::CopyChildren(const Item *From, Item *To)
 {
    Item *Parent = To;
    To->Child = new Item;
@@ -325,7 +325,7 @@ string Configuration::FindAny(const char *Name,const char *Default) const
 // Configuration::CndSet - Conditinal Set a value			/*{{{*/
 // ---------------------------------------------------------------------
 /* This will not overwrite */


и соответествующие изменения в configuration.h. (Лучше в отдельно
коммите?)

Конечно, добавление const хуже не сделает. Есть ещё такое:

diff --git a/apt/apt-pkg/contrib/progress.h b/apt/apt-pkg/contrib/progress.h
index a563d76..e9bb2ab 100644
--- a/apt/apt-pkg/contrib/progress.h
+++ b/apt/apt-pkg/contrib/progress.h
@@ -84,7 +84,7 @@ class OpTextProgress : public OpProgress
    
    OpTextProgress(bool NoUpdate = false) : NoUpdate(NoUpdate), 
                 NoDisplay(false), LastLen(0) {};
-   OpTextProgress(Configuration &Config);
+   OpTextProgress(const Configuration &Config);
    virtual ~OpTextProgress() {Done();};
 };
 
5. Использование stringstream вместо string (отдельным коммитом?)

diff --git a/apt/apt-pkg/contrib/strutl.cc b/apt/apt-pkg/contrib/strutl.cc
index c664833..732345c 100644
--- a/apt/apt-pkg/contrib/strutl.cc
+++ b/apt/apt-pkg/contrib/strutl.cc
@@ -322,26 +322,28 @@ string SubstVar(string Str,string Subst,string Contents)
 {
    string::size_type Pos = 0;
    string::size_type OldPos = 0;
-   string Temp;
+   std::stringstream Temp;
    
    while (OldPos < Str.length() && 
          (Pos = Str.find(Subst,OldPos)) != string::npos)
    {
-      Temp += string(Str,OldPos,Pos) + Contents;
+      Temp << string(Str,OldPos,Pos) << Contents;
       OldPos = Pos + Subst.length();      
    }
    
    if (OldPos == 0)
       return Str;
    
-   return Temp + string(Str,OldPos);
+   Temp << string(Str,OldPos);
+   return Temp.str();
 }
 
6. Тоже другое (в том же файле):

diff --git a/apt/apt-pkg/contrib/strutl.cc b/apt/apt-pkg/contrib/strutl.cc
index c664833..732345c 100644
--- a/apt/apt-pkg/contrib/strutl.cc
+++ b/apt/apt-pkg/contrib/strutl.cc
@@ -353,14 +355,14 @@ string URItoFileName(string URI)
 {
    // Nuke 'sensitive' items
    ::URI U(URI);
-   U.User = string();
-   U.Password = string();
-   U.Access = "";
+   U.User.clear();
+   U.Password.clear();
+   U.Access.clear();
    
    // "\x00-\x20{}|\\\\^\\[\\]<>\"\x7F-\xFF";
    URI = QuoteString(U,"\\|{}[]<>\"^~_=!@#$%^&*");
-   string::iterator J = URI.begin();
-   for (; J != URI.end(); J++)
+   auto J = URI.begin();
+   for (; J != URI.end(); ++J)
       if (*J == '/') 
         *J = '_';
    return URI;

@@ -1178,9 +1180,9 @@ URI::operator string()
 string URI::SiteOnly(string URI)
 {
    ::URI U(URI);
-   U.User = string();
-   U.Password = string();
-   U.Path = string();
+   U.User.clear();
+   U.Password.clear();
+   U.Path.clear();
    U.Port = 0;
    return U;
 }

В остальном вроде ok.

# 4ed2c0e wrap the mmap actions in the CacheGenerator in their own methods to be able to react on condition changes later then we can move mmap

Для аргумента типа string выбранная реализация, кажется, хуже
(неоптимальна):

+   unsigned long WriteStringInMap(const std::string &String) { return WriteStringInMap(String.c_str()); };


чем в аналогичном коде в mmap.h:

   inline unsigned long WriteString(string S) {return WriteString(S.c_str(),S.length());};

без необязательного параметра-длины будет лишний вызов strlen(), хотя
можно узнать значение из String.length().

# 33509fe Use references instead of copies in the Cache generation methods

В то, что ничего не портится, в этом изменении гораздо труднее
поверить или проверить, потому что не добавляется к ссылкам const.

Можно ли как-то попробовать с const это всё сделать?

# a523050 Support large files

Ещё: в apt-pkg/contrib/fileutl.h:48 Jnk лучше чтобы соответствовал
типу параметра вызываемой функции, т.е. теперь unsigned long long.

В StrToNum() char S[30] не хватит для двоичной записи 32-битного или
64-битного числа.

В остальном ok. (Конечно, в http.c sscanf() может прочитать большое
unsigned long long в StartPos, который объявлен и в других местах
используется как signed long long. Т.е. будет как бы отрицательное
число. Что произойдёт?..)

Проверили с помощью:

    find -type f '-(' -name '*.cc' -o -name '*.h' '-)' -print0 | xargs -0 sed -i -re 's:(unsigned long) long:\1:g; s:%ll:%l:g'


# 55b9b4f apt-pkg/pkgcachegen.{cc,h} changes

Может быть, здесь во всех ReMap(), чтобы не ошибиться в типах, использовать
обозначение для типов Pkg, Ver, Prv и т.п., определяющее тип по
переменной. (Где-то я видел decltype(...) или что-то в этом роде.) Ну или
шаблон, может быть. Точнее даже не чтобы не ошибиться сейчас, а чтобы в
будущем, в случае изменений оно не разъезжалось, а не тихо
пропускалось компилятором.

# 78c93fe Add and document APT::Cache-{Start,Grow,Limit} options for mmap control
# bdf8763 DynamicMMap::Grow: add optional debug output
# be0c967 Use special type to return allocation failure since 0 is a valid offset value

Почему бы не выкинуть свою реализацию, а использовать std::optional

# 112d2ea Remove ABI compat stuff

ok

# f56c14e Improve allocation failure error message
# 111ef88 Add workaround for packages with missing tags
# cfe4ad9 Bump soname
# 45b01f6 (tag: 0.5.15lorg2-alt69) 0.5.15lorg2-alt69
# 7c4fc38 Port pkgCacheFile::GetSourceList and it's dependencies from Debian
# 89457f4 Port ListUpdate function from Debian
# 99c05dc (HEAD -> sisyphus, tag: 0.5.15lorg2-alt70, darktemplar@ALT/sisyphus, darktemplar@ALT/HEAD) 0.5.15lorg2-alt70
