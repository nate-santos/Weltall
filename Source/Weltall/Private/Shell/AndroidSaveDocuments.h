#pragma once

#include "Game/SaveDocuments.h"
#include <memory>

#if PLATFORM_ANDROID
std::shared_ptr<Weltall::ISaveDocuments> MakeAndroidSaveDocuments();
#endif
