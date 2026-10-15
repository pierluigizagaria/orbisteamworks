#ifdef _WIN32
extern "C" __declspec(dllexport) void osw_fixture_marker() {}
#else
extern "C" void osw_fixture_marker() {}
#endif
