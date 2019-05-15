#ifndef APT_SCOPE_EXIT_H
#define APT_SCOPE_EXIT_H

#include <functional>
#include <utility>

class scope_exit
{
public:
   explicit scope_exit(const std::function<void()> &l_function)
      : m_function(l_function)
   {
   }

   explicit scope_exit(std::function<void()> &&l_function)
      : m_function(std::move(l_function))
   {
   }

   ~scope_exit()
   {
      try
      {
         if (m_function)
         {
            m_function();
         }
      }
      catch (...)
      {
         // ignore
      }
   }

private:
   std::function<void()> m_function;
};

#endif /* APT_SCOPE_EXIT_H */
