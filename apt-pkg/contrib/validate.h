#ifndef VALIDATE_H
#define VALIDATE_H

// for API and templates
#include <utility>
#include <optional>
#include <string>
#include <type_traits>

// for implementations
#include <apt-pkg/strutl.h>
// #include <cstdlib>
// #include <cerrno>
#include <charconv>

/* Monadic bind operation implemented for the specific family of monad
   which is a trivial wrapper around values extended with "failure".

   Unlike std::bind, it eagerly evaluates the function application.

   Assumptions:

   The underlying value is extracted from the wrapper with operator *.
   The check for "failure" is done as if it is a bool.

   A very specific assumption is that std::nullopt is the "failure" value,
   which effectively limits this implementation to the monad being
   std::optional. (Although, in theory, this implementation for mbind
   could be generalized to other similar wrapper types.)

   Unfortunately, the signature of such template functions (and their
   implementation) needs to be encumbered with "forwarding"/"universal"
   references (similarly to std::bind), an obscure C++ mechanism.
   For an explanation, see
   https://isocpp.org/blog/2012/11/universal-references-in-c11-scott-meyers .
*/
template<typename m_a, typename func_a_to_m_b>
auto mbind(const func_a_to_m_b & F,
           m_a && X)
{
   /* The sense of the expression is as simple and clear as:

      X ? F(*X) : std::nullopt

      encumbered with the obscure C++ mechanism for forwarding the value
      category, first, from the argument X to the operator *,
      then, from the result of operator * to function F.
      The goal is that the function F sees its argument
      as either rvalue or lvalue depending on whether
      our template function (mbind) saw its argument
      as either rvalue or lvalue. It's like in the examples in
      https://en.cppreference.com/w/cpp/utility/forward . Note that
      according to https://en.cppreference.com/w/cpp/language/value_category
      "a function call or an overloaded operator expression, whose
      return type is lvalue reference" is an lvalue expression (and
      that's determined by decltype); here, this is the case for the overloaded
      operator * for the case when X is an std::optional lvalue.
   */

   return X ? F(std::forward<decltype(*std::forward<m_a>(X))>(
                   *std::forward<m_a>(X)
                   ))
      : std::nullopt;
}

// "Monadic" interface to ParseQuoteWord() from strutl.h.
// (It's nice to be able to initialize local vars with the result from parser
// immediately.
// And to sequence further actions (validating the input data) with mbind().)
std::optional<std::string> ParseQuoteWord_(const char *&S)
{
   std::string Res;
   if (! ParseQuoteWord(S, Res))
      return std::nullopt;
   return Res;
}

std::optional<std::string> NonEmpty(const std::string & S)
{
   if (S.empty())
      return std::nullopt;
   return S;
}

// NonNegative - validate that the value is non-negative
// and return it as an unsigned type
//
// If the result is used as a value for a wider type, according to
// Numeric conversions, the unsigned value will be zero-extended.
// Thus, using it as unsigned long long (the widest type) is correct,
// i.e., it won't change the meaning of the value.
// (This is useful for representing file sizes as unsigned long long
// after getting them as off_t.)
template<typename t>
std::optional<std::make_unsigned_t<t>> NonNegative(const t X)
{
   if (X < 0)
      return std::nullopt;
   return static_cast<std::make_unsigned_t<t>>(X);
}

// "Monadic" interface to std::from_chars from charconv.
template<typename t>
std::optional<t> from_chars_(const std::string & S)
{
   const char * const Begin = &S[0];
   const char * const End = &S[S.length()];
   t Res;

   const std::from_chars_result Success = std::from_chars(Begin, End, Res);

   if (Success.ec != std::errc() || Success.ptr != End)
      return std::nullopt;

   return Res;
}

#endif
