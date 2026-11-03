#pragma once
#include <string>

namespace RBX { namespace LuauSyntax {
// Called only after the original compiler rejects source. Never changes saved
// script/place bytes, and never executes partially parsed source.
bool lower(const std::string& source, std::string& output, bool iterationOnly = false, std::string* diagnostic = nullptr);
} }
