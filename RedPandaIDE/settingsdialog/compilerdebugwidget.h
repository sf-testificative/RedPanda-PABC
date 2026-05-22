#ifndef COMPILERDEBUGWIDGET_H
#define COMPILERDEBUGWIDGET_H

#include <QWidget>
#include "settingswidget.h"

namespace Ui {
class compilerdebugwidget;
}

class compilerdebugwidget : public SettingsWidget
{
    Q_OBJECT

public:
    explicit compilerdebugwidget(const QString& name, const QString& group, QWidget *parent = nullptr);
    ~compilerdebugwidget();

private:
    Ui::compilerdebugwidget *ui;
protected:
    void doLoad() override;
    void doSave() override;
private slots:
    void on_debugMode_stateChanged(int arg1);
};

#endif // COMPILERDEBUGWIDGET_H
