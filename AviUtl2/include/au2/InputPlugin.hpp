#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmsystem.h>

#include "input2.h"

namespace au2 {

bool initialize_plugin(DWORD version) noexcept;
void uninitialize_plugin() noexcept;
INPUT_PLUGIN_TABLE* input_plugin_table() noexcept;

} // namespace au2
