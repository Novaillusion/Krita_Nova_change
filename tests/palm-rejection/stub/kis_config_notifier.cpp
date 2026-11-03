#include "kis_config_notifier.h"
KisConfigNotifier *KisConfigNotifier::instance() { static KisConfigNotifier n; return &n; }
