#include "intellisensemanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QJsonArray>
#include <zmq.hpp>
#include "qsynedit/qsynedit.h"
#include "editor.h"
#include "compiler/externalcompilermanager.h"

IntelliSenseManager::IntelliSenseManager(QObject *parent)
    : QObject{parent}, requestId(0), context(1),
    requester(context, zmq::socket_type::req)
{
    intelliProcess = new QProcess(this);
    requester.connect("tcp://127.0.0.1:5557");
    requester.set(zmq::sockopt::reconnect_ivl, 100);    // 100ms between retries
    requester.set(zmq::sockopt::reconnect_ivl_max, 5000); // Max 5s delay
}

IntelliSenseManager& IntelliSenseManager::instance()
{
    static IntelliSenseManager instance(nullptr);
    return instance;
}

void IntelliSenseManager::connectEvents() {
    initializeLSP("");
}

void IntelliSenseManager::initializeLSP(const QString& filename) {
    QJsonObject lspParams;
    lspParams["processId"] = static_cast<int>(QCoreApplication::applicationPid());

    //QString canonicalPath = QFileInfo(filename).canonicalFilePath();
    //QUrl rootUrl = QUrl::fromLocalFile(canonicalPath.isEmpty() ? filename : canonicalPath);
    QString canonicalPath = QFileInfo(filename).absoluteFilePath();
    QUrl rootUrl = QUrl::fromLocalFile(canonicalPath);
    lspParams["rootUri"] = rootUrl.toString();

    // Define capabilities
    QJsonObject synchronization{
        {"didSave", true},
        {"willSave", true},
        {"willSaveWaitUntil", false}
    };

    QJsonObject completionItem{
        {"snippetSupport", true}
    };

    QJsonObject textDocument{
        {"synchronization", synchronization},
        {"completion", QJsonObject{{"completionItem", completionItem}}}
    };

    QJsonObject capabilities{
        {"workspace", QJsonObject{{"applyEdit", true}}},
        {"textDocument", textDocument}
    };

    lspParams["capabilities"] = capabilities;

    QJsonObject resultJSON{
        {"jsonrpc", "2.0"},
        {"id", requestId++},
        {"method", "initialize"},
        {"params", lspParams}
    };

    sendMessage(QJsonDocument(resultJSON).toJson(QJsonDocument::Compact).toStdString());
}

QStringList IntelliSenseManager::callIntelli(const QSynedit::BufferCoord& pos, const QString& filename, const QString& request_type) {
    QJsonObject lspPosition{
        {"line", pos.line - 1},
        {"character", pos.ch - 1}
    };

    //QString canonicalPath = QFileInfo(filename).canonicalFilePath();
    //QJsonObject textDocument{
    //    {"uri", QUrl::fromLocalFile(canonicalPath.isEmpty() ? filename : canonicalPath).toString()}
    //};
    QString canonicalPath = QFileInfo(filename).absoluteFilePath();
    QJsonObject textDocument{
        {"uri", QUrl::fromLocalFile(canonicalPath).toString()}
    };

    QJsonObject resultJSON{
        {"jsonrpc", "2.0"},
        {"id", requestId++},
        {"method", "textDocument/" + request_type},
        {"params", QJsonObject{
                       {"textDocument", textDocument},
                       {"position", lspPosition}
                   }}
    };

    QString data = QJsonDocument(resultJSON).toJson(QJsonDocument::Compact);
    QString result = sendMessage(data.toStdString());

    if (request_type == "hover") {
        return QStringList() << QJsonDocument::fromJson(result.toUtf8()).object()["result"].toObject()["contents"].toObject()["value"].toString();
    } else if (request_type == "completion") {
        QStringList res;
        QJsonArray resultArray = QJsonDocument::fromJson(result.toUtf8()).object()["result"].toArray();

        for (const QJsonValue& item : resultArray) {
            if (item.isObject() && item.toObject().contains("label")) {
                res << item.toObject()["label"].toString();
            }
        }
        return res;
    }
    return QStringList() << "Nothing really happened.";
}

void IntelliSenseManager::didChange(const QString& filename, const QString& fulltext) {
    if (!documentVersions.contains(filename)) {
        documentVersions[filename] = 0;
    }

    QJsonObject message{
        {"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params", QJsonObject{
                        {"textDocument", QJsonObject{
                                             //{"uri", [&](){
                                             //    QString cp = QFileInfo(filename).canonicalFilePath();
                                             //    return QUrl::fromLocalFile(cp.isEmpty() ? filename : cp).toString();
                                             //}()},
                                             {"uri", QUrl::fromLocalFile(QFileInfo(filename).absoluteFilePath()).toString()},
                                            {"version", ++documentVersions[filename]}
                                        }},
                       {"contentChanges", QJsonArray{
                                              QJsonObject{{"text", fulltext}}
                                          }}
                   }}
    };

    sendMessage(QJsonDocument(message).toJson(QJsonDocument::Compact).toStdString());
}

void IntelliSenseManager::startIntelli() {
#ifdef Q_OS_WINDOWS
    QString path_to_pas = QCoreApplication::applicationDirPath() + "\\..\\PascalABCNETLinux\\LSPProxy\\TestIntelli.exe";
    intelliProcess->setProgram(path_to_pas);
    intelliProcess->setProcessChannelMode(QProcess::SeparateChannels);
    intelliProcess->setArguments(QStringList() << "/noconsole" << "commandmode");
#else
    QString path_to_mono = "mono";
    intelliProcess->setProgram(path_to_mono);
    QString path_to_pas = ExternalCompilerManager::instance().findPascalABCNET("LSPProxy/TestIntelli.exe");
    intelliProcess->setArguments(QStringList() << path_to_pas);
#endif

    intelliProcess->start();
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [this]() {
        if (intelliProcess->state() == QProcess::Running) {
            intelliProcess->terminate();
        }});

    if (!intelliProcess->waitForStarted()) {
        qDebug() << "Failed to start process!";
    } else {
        qDebug() << "INTELLISENSE Process started successfully.";
    }
}

void IntelliSenseManager::didOpen(const QString& filename, Editor* editor) {
    //QString canonicalPath = QFileInfo(filename).canonicalFilePath();
    //if (!canonicalPath.isEmpty())
    //    editor->setFilename(canonicalPath);
    //else
    //    return;
    QString canonicalPath = QFileInfo(filename).absoluteFilePath();
    if (!canonicalPath.isEmpty())
        editor->setFilename(canonicalPath);
    //QUrl fileUrl = QUrl::fromLocalFile(canonicalPath.isEmpty() ? filename : canonicalPath);
    //QUrl fileUrl = QUrl::fromLocalFile(canonicalPath);
    QUrl fileUrl = QUrl::fromLocalFile(canonicalPath);

    QJsonObject textDocument{
        {"uri", fileUrl.toString()},
        {"languageId", "pas"},
        {"version", 0},
        {"text", editor->text()}
    };

    QJsonObject resultJSON{
        {"jsonrpc", "2.0"},
        {"method", "textDocument/didOpen"},
        {"params", QJsonObject{{"textDocument", textDocument}}}
    };

    sendMessage(QJsonDocument(resultJSON).toJson(QJsonDocument::Compact).toStdString());

    connect(editor, &QSynedit::QSynEdit::changeForIntelli, this, [this, filename](const QString& text) {
        didChange(filename, text);
    });
}

QString IntelliSenseManager::sendMessage(const std::string& message) {
    try {
        zmq::message_t msg(message.size());
        memcpy(msg.data(), message.c_str(), message.size());
        requester.send(msg, zmq::send_flags::none);

        zmq::pollitem_t items[] = {{static_cast<void*>(requester), 0, ZMQ_POLLIN, 0}};
        zmq::poll(items, 1, 10000);

        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t reply;
            if (requester.recv(reply)) {
                std::string replyMessage(static_cast<char*>(reply.data()), reply.size());
                return QString::fromStdString(replyMessage);
            }
        }
    } catch (...) {
        qDebug() << "Unknown error occured!";
    }
    return "";
}
