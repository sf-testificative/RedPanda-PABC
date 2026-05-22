
#include "../settings.h"
#include "compilerdebugwidget.h"
#include "ui_compilerdebugwidget.h"

compilerdebugwidget::compilerdebugwidget(const QString& name, const QString& group, QWidget* parent)
    : SettingsWidget(name, group, parent)
    , ui(new Ui::compilerdebugwidget)
{
    ui->setupUi(this);
}

compilerdebugwidget::~compilerdebugwidget()
{
    delete ui;
}

void compilerdebugwidget::doLoad() {
    ui->debugMode->setChecked(pSettings->compiler().getDebugMode());
}
void compilerdebugwidget::doSave() {
    pSettings->compiler().setDebugMode(ui->debugMode->isChecked());

    pSettings->compiler().save();
}

void compilerdebugwidget::on_debugMode_stateChanged(int arg1)
{

}



