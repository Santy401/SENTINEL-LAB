#pragma once
#include "rf/event_engine.h"
namespace sentinel { namespace rf {
void beginNotifier(const char* bootId);
void queueNotification(const Event& event);
bool notifierEnabled();
} }
