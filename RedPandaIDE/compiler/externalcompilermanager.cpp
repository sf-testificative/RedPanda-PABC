#include "externalcompilermanager.h"

#include <QDebug>
#include <QApplication>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QFileInfo>

#include "mainwindow.h"
#include "settings.h"

ExternalCompilerManager& ExternalCompilerManager::instance()
{
    static ExternalCompilerManager instance(nullptr);
    return instance;
}

ExternalCompilerManager::ExternalCompilerManager(QObject* parent)
    : QObject(parent),
    context(1),
    requester(context, zmq::socket_type::req)
{
    compilerProcess = new QProcess(this);
    adapterProcess = new QProcess(this);
    requester.connect("tcp://127.0.0.1:5555");
}

ExternalCompilerManager::~ExternalCompilerManager()
{
    if (compilerProcess->state() == QProcess::Running) {
        compilerProcess->terminate();
        compilerProcess->waitForFinished(3000);
        if (compilerProcess->state() == QProcess::Running) {
            compilerProcess->kill();
        }
    }

    if (adapterProcess->state() == QProcess::Running) {
        adapterProcess->terminate();
        adapterProcess->waitForFinished(3000);
        if (adapterProcess->state() == QProcess::Running) {
            adapterProcess->kill();
        }
    }

    requester.close();
    context.close();
}

QString ExternalCompilerManager::findAdapter(const QString& exename)
{
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/../../usr/share/RedPandaCPP/adapter/" + exename,
        QCoreApplication::applicationDirPath() + "/" + exename,
        QCoreApplication::applicationDirPath() + "/adapter/" + exename,
        QCoreApplication::applicationDirPath() + "/../adapter/" + exename,
        QStandardPaths::findExecutable(exename)
    };

    for (const QString& path : possiblePaths) {
        QFileInfo fi(path);
        if (fi.exists() && fi.isFile()) {
            qDebug() << "Found adapter at:" << fi.absoluteFilePath();
            return fi.absoluteFilePath();
        }
    }
    return QString();
}

QString ExternalCompilerManager::findPascalABCNET(const QString& exename)
{
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/../../" +
            QString("%1/share/%2/PascalABCNETLinux").arg(PREFIX).arg(APP_NAME),
        QCoreApplication::applicationDirPath() + "/../../../PascalABCNETLinux"
    };

    for (const QString& path : possiblePaths) {
        QFile file(path + "/" + exename);
        if (file.exists()) {
            return file.fileName();
        }
    }
    return QString();
}

void ExternalCompilerManager::resetZmqSocket()
{
    try {
        requester.close();
        requester = zmq::socket_t(context, zmq::socket_type::req);
        requester.connect("tcp://127.0.0.1:5555");
        qDebug() << "ZMQ socket recreated and connected";
    } catch (const std::exception& e) {
        qDebug() << "Failed to reset ZMQ socket:" << e.what();
    }
}

void ExternalCompilerManager::startCompiler()
{
    QString adapterPath = findAdapter("ConsoleAdapter");
    QString monoPath = QStandardPaths::findExecutable("mono");
    if (monoPath.isEmpty()) {
        qDebug() << "Mono not found!";
        return;
    }

    if (adapterPath.isEmpty()) {
        qDebug() << "Adapter not found! Falling back to direct compiler...";
#ifdef Q_OS_WINDOWS
        QString path_to_pas = "D:\\Sci\\pascalabcnet-zmq\\bin\\pabcnetc.exe";
        compilerProcess->setProgram(path_to_pas);
        compilerProcess->setProcessChannelMode(QProcess::SeparateChannels);
        compilerProcess->setArguments(QStringList() << "/noconsole" << "commandmode");
#else
        QString path_to_pas = findPascalABCNET("pabcnetc.exe");
        compilerProcess->setProgram(monoPath);
        compilerProcess->setProcessChannelMode(QProcess::SeparateChannels);
        compilerProcess->setArguments(QStringList() << path_to_pas << "/noconsole" << "commandmode");
#endif
        compilerProcess->start(QProcess::ReadWrite);
        if (!compilerProcess->waitForStarted()) {
            qDebug() << "Failed to start direct compiler!";
        }
        return;
    }

    qDebug() << "Starting native adapter:" << adapterPath;

    adapterProcess->setProgram(monoPath);
    adapterProcess->setArguments(QStringList() << adapterPath);
    adapterProcess->setWorkingDirectory(QFileInfo(adapterPath).absolutePath());
    adapterProcess->setProcessChannelMode(QProcess::SeparateChannels);
    adapterProcess->start(QProcess::ReadWrite);

    if (!adapterProcess->waitForStarted(5000)) {
        qDebug() << "Failed to start native adapter!";
        return;
    }

    qDebug() << "Adapter started with PID:" << adapterProcess->processId();
    QThread::sleep(2);

    if (adapterProcess->state() != QProcess::Running) {
        qDebug() << "Adapter crashed after start! Exit code:" << adapterProcess->exitCode();
        return;
    }

    qDebug() << "Adapter startup sequence completed.";
}

void ExternalCompilerManager::killCompiler()
{
    if (adapterProcess && adapterProcess->state() == QProcess::Running) {
        adapterProcess->terminate();
        if (!adapterProcess->waitForFinished(3000)) {
            adapterProcess->kill();
        }
    }

    if (compilerProcess && compilerProcess->state() == QProcess::Running) {
        compilerProcess->kill();
    }
}

void ExternalCompilerManager::restartCompiler()
{
    qDebug() << "Restarting compiler...";
    killCompiler();
    compilerProcess->waitForFinished();
    if (adapterProcess) {
        adapterProcess->waitForFinished();
    }
    resetZmqSocket();
    startCompiler();
}

void ExternalCompilerManager::sendMessage(const std::string& message)
{
    try {

        zmq::message_t msg(message.size());
        memcpy(msg.data(), message.c_str(), message.size());
        requester.send(msg, zmq::send_flags::none);

        zmq::pollitem_t items[] = {{static_cast<void*>(requester), 0, ZMQ_POLLIN, 0}};
        zmq::poll(items, 1, 15000);

        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t reply;
            if (requester.recv(reply)) {
                QString replyMessage = QString::fromStdString(
                    std::string(static_cast<char*>(reply.data()), reply.size()));

                pMainWindow->logToolsOutput(replyMessage);
                if (replyMessage.startsWith("100")) {
                    pMainWindow->logToolsOutput("COMPILED SUCCESSFULLY!");
                } else {
                    error(replyMessage);
                }
            } else {
                QMessageBox::warning(pMainWindow, "Error",
                                     "Failed to receive response.");
            }
        } else {
            QMessageBox::warning(pMainWindow, "Error",
                                 "No response received within the timeout period.");
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(pMainWindow, "Error",
                              QString("Communication error: %1").arg(e.what()));
    }
}

void ExternalCompilerManager::error(const QString& msg)
{
    QRegularExpression regex(R"(\[0\]\[(\d+),(\d+)\]\s+((?:[A-Za-z]:)?[^:]+):\s+(.*?)(?=\s*\[\d+\]|$))");
    QRegularExpressionMatchIterator i = regex.globalMatch(msg);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        if (match.hasMatch()) {
            PCompileIssue issue = std::make_shared<CompileIssue>();
            issue->line = match.captured(1).toInt();
            issue->column = match.captured(2).toInt();
            issue->filename = match.captured(3);
            issue->description = match.captured(4);
            issue->type = CompileIssueType::Error;
            pMainWindow->onCompileIssue(issue);
        }
    }
}

void ExternalCompilerManager::compile(const QString& filepath)
{
    std::string message = "215#5#" + filepath.toStdString();
    qDebug() << QString::fromStdString(message);
    sendMessage(message);
    std::string debugCmd = "212 " + std::to_string(debugMode);
    qDebug() << QString::fromStdString(debugCmd);
    sendMessage(debugCmd);
    sendMessage("210");
    pMainWindow->onCompileFinished(filepath, true);
}

void ExternalCompilerManager::scheduleRestart(int msecs)
{
    timer.setInterval(msecs);
    connect(&timer, &QTimer::timeout, this, &ExternalCompilerManager::restartCompiler);
    timer.start();
}
