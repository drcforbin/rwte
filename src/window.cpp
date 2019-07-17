#include "rwte/window.h"

WindowError::WindowError(const std::string& arg) :
    std::runtime_error(arg)
{ }

WindowError::WindowError(const char* arg) :
    std::runtime_error(arg)
{ }

WindowError::~WindowError()
{ }
