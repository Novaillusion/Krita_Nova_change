// Test stub: KisConfig returning the defaults passed to readEntry(),
// unless overridden in KisConfig::overrides.
#pragma once
#include <QString>
#include <QVariant>
#include <QMap>

class KisConfig
{
public:
    explicit KisConfig(bool) {}
    template<class T> T readEntry(const QString &name, const T &defaultValue = T())
    {
        return overrides().contains(name) ? overrides()[name].value<T>() : defaultValue;
    }
    static QMap<QString, QVariant> &overrides() { static QMap<QString, QVariant> m; return m; }
};
