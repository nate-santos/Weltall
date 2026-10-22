#pragma once

#if PLATFORM_ANDROID
/** Opens a user-chosen JSON document; never uploads or includes save/ROM contents. */
bool ExportAndroidDiagnostics();
#endif
