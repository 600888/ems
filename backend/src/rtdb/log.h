#pragma once 
#include "util/log_base.h"

#ifndef LOG_INIT
static LogInitializer logInitializer("rtdb", LV::debug, false, 1);
#define LOG_INIT
#endif