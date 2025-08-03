#pragma once

#include <cstdint>
#include <quill/Logger.h>
#include <string>
#include <system_error>
#include <variant>

struct OpenGLError {
    uint32_t error_code;
};

struct ShadercError {
    std::string message;
};

struct GLTFError {
    std::string error_msg;
    std::string warning_msg;
};

struct SystemError {
    std::error_code e;
};

using GenericProgramError = std::variant<std::monostate, OpenGLError,
                                         ShadercError, SystemError, GLTFError>;

void log_program_error(quill::Logger* logger, const GenericProgramError& err);
