#pragma once

#include <QObject>
#include <QTcpServer>
#include <QVariantMap>

class AgentService;

/**
 * 本地 HTTP 接口（v0.6 新增）：
 * 只监听 127.0.0.1，把 AgentService 的工具暴露给外部 AI Agent。
 *
 *   GET  /status        → 软件状态
 *   GET  /tools         → 能力发现（全部工具与参数）
 *   POST /call          → 调用工具，body: {"tool":"...","args":{...},"source":"..."}
 *
 * 端口：默认 8618，被占用时依次向后尝试至 8622；
 * 实际端口以启动后状态栏显示为准。
 */
class AgentHttpServer : public QObject
{
    Q_OBJECT
public:
    explicit AgentHttpServer(AgentService *svc, QObject *parent = nullptr);

    /// 尝试绑定端口并开始监听；返回实际端口（0 表示失败）
    quint16 start();

    quint16 port() const { return port_; }
    bool isRunning() const { return port_ != 0; }

private:
    void onConnection();
    void handleRequest(QTcpSocket *sock, const QByteArray &method, const QByteArray &path,
                       const QByteArray &body);
    static QByteArray httpResponse(int code, const QString &reason, const QByteArray &json);

    AgentService *svc_ = nullptr;
    QTcpServer server_;
    quint16 port_ = 0;
};
