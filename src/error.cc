#include "error.hpp"
#include <quill/LogMacros.h>
#include <quill/Logger.h>

template<typename... Visitors>
struct VariantVisitor : public Visitors...
{
    VariantVisitor()
        : Visitors{}...
    {
    }
    VariantVisitor(Visitors&&... visitors)
        : Visitors{ std::forward<Visitors>(visitors) }...
    {
    }

    using Visitors::operator()...;
};

void
log_program_error(quill::Logger* logger, const GenericProgramError& err)
{
    std::visit(VariantVisitor{
                   [logger](const OpenGLError& gl_err) {},
                   [logger](const ShadercError& sh_err) {},
                   [logger](const SystemError& sys_err) {},
                   [logger](const GLTFError& e) {},
                   [logger](std::monostate) {},
               },
               err);
}
