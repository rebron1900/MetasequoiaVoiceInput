#pragma once

#include <string>

namespace mvi_logger
{
void Initialize(const std::string &path, bool enabled);
void Write(const std::string &category, const std::string &message);
}
