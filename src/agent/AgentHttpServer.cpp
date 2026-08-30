#include "agent/AgentHttpServer.h"
#include "agent/AgentService.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {
constexpr int kPortFirst = 8618;
constexpr int kPortLast = 8622;
}

AgentHttpServer::AgentHttpServer(AgentService *svc, QObject *parent)
    : QObject(parent)
    , svc_(svc)
{
    connect(&server_, &QTcpServer::newConnection, this, &AgentHttpServer::onConnection);
}

quint16 AgentHttpServer::start()
{
    for (int p = kPortFirst; p <= kPortLast; ++p) {
        if (server_.listen(QHostAddress::LocalHost, quint16(p))) {
            port_ = quint16(p);
            return port_;
        }
    }
    port_ = 0;
    return 0;
}

void AgentHttpServer::onConnection()
{
    while (server_.hasPendingConnections()) {
        QTcpSocket *sock = server_.nextPendingConnection();
        connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);

        // 简单协议：一连接一请求。收齐头部后再按 Content-Length 收 body。
        auto *buf = new QByteArray;
        auto *gotHeader = new bool(false);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, buf, gotHeader] {
            buf->append(sock->readAll());
            if (!*gotHeader) {
                const int headEnd = buf->indexOf("\r\n\r\n");
                if (headEnd < 0)
                    return;

                // 解析 Content-Length（头字段大小写不敏感）
                int contentLength = 0;
                const QByteArray head = buf->left(headEnd);
                const QList<QByteArray> lines = head.split('\n');
                for (const QByteArray &line : lines) {
                    if (line.left(15).toLower() == "content-length:") {
                        contentLength = line.mid(15).trimmed().toInt();
                        break;
                    }
                }

                const int need = headEnd + 4 + contentLength;
                if (buf->size() < need)
                    return;   // body 未收齐，继续等
                *gotHeader = true;

                const QByteArray requestLine = lines.value(0).trimmed();
                const QList<QByteArray> parts = requestLine.split(' ');
                const QByteArray method = parts.value(0);
                QByteArray path = parts.value(1);
                const int qm = path.indexOf('?');
                if (qm >= 0)
                    path.truncate(qm);
                const QByteArray body = buf->mid(headEnd + 4, contentLength);

                handleRequest(sock, method, path, body);
                sock->disconnectFromHost();   // 响应写完后关闭（一连接一请求）
            }
        });
        connect(sock, &QTcpSocket::errorOccurred, sock, &QTcpSocket::deleteLater);
    }
}

void AgentHttpServer::handleRequest(QTcpSocket *sock, const QByteArray &method,
                                    const QByteArray &path, const QByteArray &body)
{
    QVariantMap resp;

    if (path == "/status" && method == "GET") {
        resp = svc_->status();
        resp.insert(QStringLiteral("http"), QStringLiteral("shengku.agent.v1"));
    } else if ((path == "/tools" || path == "/capabilities") && method == "GET") {
        resp = svc_->capabilities();
    } else if (path == "/call" && method == "POST") {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            resp = {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"),
                     QStringLiteral("bad_json:%1").arg(err.errorString())}};
        } else {
            const QVariantMap req = doc.object().toVariantMap();
            const QString tool = req.value(QStringLiteral("tool")).toString();
            const QVariantMap args = req.value(QStringLiteral("args")).toMap();
            QString source = req.value(QStringLiteral("source")).toString();
            if (source.isEmpty())
                source = QStringLiteral("http-agent");
            resp = svc_->call(tool, args, source);
        }
    } else if (method == "GET" && (path == "/" || path.isEmpty())) {
        resp = {{QStringLiteral("app"), QStringLiteral("声库")},
                {QStringLiteral("hint"), QStringLiteral("GET /status · GET /tools · POST /call")},
                {QStringLiteral("docs"), QStringLiteral("见安装目录 data/声库-AI接入卡.md")}};
    } else {
        resp = {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("not_found")}};
        sock->write(httpResponse(404, QStringLiteral("Not Found"),
                                 QJsonDocument(QJsonObject::fromVariantMap(resp)).toJson()));
        return;
    }

    sock->write(httpResponse(200, QStringLiteral("OK"),
                             QJsonDocument(QJsonObject::fromVariantMap(resp)).toJson()));
}

QByteArray AgentHttpServer::httpResponse(int code, const QString &reason, const QByteArray &json)
{
    QByteArray out;
    out += QByteArray("HTTP/1.1 ") + QByteArray::number(code) + ' ' + reason.toUtf8() + "\r\n";
    out += "Content-Type: application/json; charset=utf-8\r\n";
    out += "Access-Control-Allow-Origin: *\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "Connection: close\r\n";
    out += "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n";
    out += json;
    return out;
}
