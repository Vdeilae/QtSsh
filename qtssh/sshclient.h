#pragma once

#include <QObject>
#include <QList>
#include <QTcpSocket>
#include <QTimer>
#include <QMutex>
#include "sshchannel.h"
#include "sshkey.h"
#include <QSharedPointer>

#ifndef FALLTHROUGH
#if __has_cpp_attribute(fallthrough)
#define FALLTHROUGH [[fallthrough]]
#elif __has_cpp_attribute(clang::fallthrough)
#define FALLTHROUGH [[clang::fallthrough]]
#else
#define FALLTHROUGH
#endif
#endif

Q_DECLARE_LOGGING_CATEGORY(sshclient)
class SshProcess;
class SshScpGet;
class SshScpSend;
class SshSFtp;
class SshTunnelIn;
class SshTunnelOut;

class  SshClient : public QObject {
    Q_OBJECT

public:
    enum SshState {
        Unconnected,
        SocketConnection,
        WaitingSocketConnection,
        Initialize,
        HandShake,
        GetAuthenticationMethodes,
        Authentication,
        Ready,
        DisconnectingChannel,
        DisconnectingSession,
        FreeSession,
        Error,
    };
    Q_ENUM(SshState)

private:
    static int s_nbInstance;
    LIBSSH2_SESSION    * m_session {nullptr};
    LIBSSH2_KNOWNHOSTS * m_knownHosts {nullptr};
    QList<SshChannel*> m_channels;

    QString m_name;
    QTcpSocket m_socket;
    qint64 m_lastProofOfLive {0};

    quint16 m_port {0};
    int m_errorcode {0};
    bool m_sshConnected {false};

    QString m_hostname;
    QString m_username;
    QString m_passphrase;
    QString m_privateKey;
    QString m_publicKey;
    QString m_errorMessage;
    QString m_knowhostFiles;
    SshKey  m_hostKey;
    QTimer m_keepalive;
    QTimer m_connectionTimeout;
    QMutex channelCreationInProgress;
    void *currentLockerForChannelCreation {nullptr};

public:
    SshClient(const QString &name = "noname", QObject * parent = nullptr);
    virtual ~SshClient();

    QString getName() const;
    bool takeChannelCreationMutex(void *identifier);
    void releaseChannelCreationMutex(void *identifier);



public slots:
    int connectToHost(const QString & username, const QString & hostname, quint16 port = 22, QByteArrayList methodes = QByteArrayList());
    bool waitForState(SshClient::SshState state);
    void disconnectFromHost();

public:
    template<typename T>
    T *getChannel(const QString &name)
    {
        for(SshChannel *ch: m_channels)
        {
            if(ch->name() == name)
            {
                T *proc = qobject_cast<T*>(ch);
                if(proc)
                {
                    return proc;
                }
            }
        }

        T *res = new T(name, this);
        m_channels.append(res);
        QObject::connect(res, &SshChannel::stateChanged, this, &SshClient::_channel_free);
        emit channelsChanged(m_channels.count());
        return res;
    }

    void setKeys(const QString &publicKey, const QString &privateKey);
    void setPassphrase(const QString & pass);
    bool saveKnownHosts(const QString &file);
    void setKownHostFile(const QString &file);
    bool addKnownHost  (const QString &hostname, const SshKey &key);
    QString banner();


    LIBSSH2_SESSION *session();



private slots:
    void _sendKeepAlive();


public: /* New function implementation with state machine */
    SshState sshState() const;

    void setName(const QString &name);

private: /* New function implementation with state machine */
    SshState m_sshState {SshState::Unconnected};
    QByteArrayList m_authenticationMethodes;
    void setSshState(const SshState &sshState);


private slots: /* New function implementation with state machine */
    void _connection_socketTimeout();
    void _connection_socketError();
    void _connection_socketConnected();
    void _connection_socketDisconnected();
    void _ssh_processEvent();
    void _channel_free();

signals:
    void sshStateChanged(SshState sshState);
    void sshReady();
    void sshDisconnected();
    void sshError();

    void sshDataReceived();
    void sshEvent();
    void channelsChanged(int);
};
