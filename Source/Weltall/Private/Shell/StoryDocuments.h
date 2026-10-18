#pragma once

#include "Story/StoryPacks.h"

/** Read authored story patches through the platform's file layer. */
Weltall::TResult<Weltall::FStoryPacks> ReadStoryDocuments();
