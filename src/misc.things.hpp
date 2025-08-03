#pragma once

#include <utility>

#define STRINGIZE_(x) #x
#define PASTE(x, y) x##y
#define PASTE_FWD(x, y) PASTE(x, y)
#define MAKE_AUTOVAR PASTE_FWD(local_guard, __LINE__)

template<typename Fn>
class Finally
{
  public:
    explicit Finally(const Fn& fn)
        : _fn{ fn }
    {
    }

    explicit Finally(Fn&& fn)
        : _fn{ std::move(fn) }
    {
    }

    ~Finally() { _fn(); }

  private:
    Finally(const Finally&) = delete;
    Finally& operator=(const Finally&) = delete;

    Fn _fn;
};

#define SCOPED_GUARD(expr)                                                                                             \
    auto MAKE_AUTOVAR = Finally                                                                                        \
    {                                                                                                                  \
        expr                                                                                                           \
    }

template<typename... Visitors>
struct VariantVisitor : Visitors...
{
    using Visitors::operator()...;
};
