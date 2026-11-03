#pragma once
#include <QObject>

class KisConfigNotifier : public QObject
{
    Q_OBJECT
public:
    static KisConfigNotifier *instance();
    void notifyConfigChanged() { Q_EMIT configChanged(); }
Q_SIGNALS:
    void configChanged();
};
