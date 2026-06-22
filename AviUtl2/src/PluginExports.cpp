#include "au2/InputPlugin.hpp"

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version)
{
    return au2::initialize_plugin(version);
}

EXTERN_C __declspec(dllexport) void UninitializePlugin()
{
    au2::uninitialize_plugin();
}

EXTERN_C __declspec(dllexport) INPUT_PLUGIN_TABLE* GetInputPluginTable(void)
{
    return au2::input_plugin_table();
}
