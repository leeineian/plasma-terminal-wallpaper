#include <QQmlExtensionPlugin>
#include <qqml.h>
#include "terminal.h"

class TermPlugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
public:
    void registerTypes(const char *uri) override {
        Q_UNUSED(uri);
        qmlRegisterType<Terminal>("com.wallpaper.terminal", 1, 0, "Terminal");
    }
};

#include "plugin.moc"
