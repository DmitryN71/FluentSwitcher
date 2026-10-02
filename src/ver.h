#include "fs_version.h" // made by CMake: FLUENTSWITCHER_VERSION

namespace details {
	static const char* SW_VERSION = FS_VERSION;
}

// The version of FluentSwitcher ("1.0.0"); SimpleSwitcher added " PREVIEW" to its nightly builds.
inline const char* GET_SW_VERSION() {
	return details::SW_VERSION;
}
