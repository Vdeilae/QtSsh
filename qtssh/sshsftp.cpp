#include "sshsftp.h"
#include "sshclient.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>

Q_LOGGING_CATEGORY(logsshsftp, "ssh.sftp", QtWarningMsg)

SshSFtp::SshSFtp(const QString &name, SshClient *client):
    SshChannel(name, client)

{
    while(!(m_sftpSession = libssh2_sftp_init(m_sshClient->session())))
    {
        if(libssh2_session_last_errno(m_sshClient->session()) == LIBSSH2_ERROR_EAGAIN)
        {
            QCoreApplication::processEvents();
        }
        else
        {
            break;
        }
    }
    if(!m_sftpSession)
    {
        qCWarning(logsshsftp) << "LAST ERROR IS : " << libssh2_session_last_errno(m_sshClient->session());
    }
    QObject::connect(client, &SshClient::sshDataReceived, this, &SshSFtp::sshDataReceived, Qt::QueuedConnection);

    qCDebug(logsshsftp) << "SFTP connected";
}

SshSFtp::~SshSFtp()
{
    qCDebug(logsshsftp) << "SshSFtp::~SshSFtp()";
    close();
    qCDebug(logsshsftp) << "SshSFtp::~SshSFtp() OK";
}

void SshSFtp::close()
{
    for(LIBSSH2_SFTP_HANDLE *sftpdir: m_dirhandler)
    {
        libssh2_sftp_closedir(sftpdir);
    }
    m_dirhandler.clear();

    if(m_sftpSession)
    {
        libssh2_sftp_shutdown(m_sftpSession);
        m_sftpSession = nullptr;
        qCDebug(sshchannel) << "closeChannel:" << m_name;
    }
}

void SshSFtp::free()
{

}


QString SshSFtp::send(const QString &source, QString dest)
{
    QFile s(source);
    QFileInfo src(source);
    QByteArray buffer(1024 * 100, 0);
    size_t nread;
    char *ptr;
    long total = 0;
    FILE *local;
    int rc;
    LIBSSH2_SFTP_HANDLE *sftpfile;

    if(dest.endsWith("/"))
    {
        if(!isDir(dest))
        {
            mkpath(dest);
        }
        dest += src.fileName();
    }

    local = fopen(qPrintable(source), "rb");
    if (!local) {
        qCWarning(logsshsftp) << "Can't open file "<< source;
        return "";
    }


    do {
        sftpfile = libssh2_sftp_open(m_sftpSession, qPrintable(dest),
                                 LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
                                     LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR|
                                                               LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IROTH);
        rc = libssh2_session_last_errno(m_sshClient->session());
        if (!sftpfile && (rc == LIBSSH2_ERROR_EAGAIN))
        {
            m_waitData(2000);
        }
        else
        {
            if(!sftpfile)
            {
                qCWarning(logsshsftp) << "SSH error " << rc;
                fclose(local);
                return "";
            }
        }
    } while (!sftpfile);

    do {
         nread = fread(buffer.data(), 1, buffer.size(), local);
         if (nread <= 0) {
             /* end of file */
             break;
         }
         ptr = buffer.data();

         total += nread;

         do {
             while ((rc = libssh2_sftp_write(sftpfile, ptr, nread)) == LIBSSH2_ERROR_EAGAIN)
             {
                 m_waitData(2000);
             }
             if(rc < 0)
             {
                 qCWarning(logsshsftp) << "Write error send(" << source << "," <<  dest << ") = " << rc;
                 break;
             }
             ptr += rc;
             nread -= rc;

         } while (nread);
     } while (rc > 0);

    fclose(local);
    libssh2_sftp_close(sftpfile);

    return dest;
}

bool SshSFtp::get(const QString &source, QString dest, bool override)
{
    QFileInfo src(source);
    LIBSSH2_SFTP_HANDLE *sftpfile;
    int retry = 5;
    QByteArray buffer(1024 * 100, 0);
    FILE *tempstorage;
    int rc;

    if(dest.endsWith("/"))
    {
        dest += src.fileName();
    }
    QString original(dest);

    if(!override)
    {
        QFile fout(dest);
        if(fout.exists())
        {
            QString newpath;
            int i = 1;
            do {
                newpath = QString("%1.%2").arg(dest).arg(i);
                fout.setFileName(newpath);
                ++i;
            } while(fout.exists());
            dest = newpath;
        }
    }

    tempstorage = fopen(qPrintable(dest), "w");
    if (!tempstorage) {
        qCWarning(logsshsftp) << "Can't open file "<< dest;
        return false;
    }

    do {
        sftpfile = libssh2_sftp_open(m_sftpSession, qPrintable(source), LIBSSH2_FXF_READ, 0);
        rc = libssh2_session_last_errno(m_sshClient->session());
        if (!sftpfile && (rc == LIBSSH2_ERROR_EAGAIN))
        {
            m_waitData(2000);
        }
        else
        {
            if(!sftpfile)
            {
                if(retry-- > 0)
                {
                    m_waitData(2000);
                }
                else
                {
                    qCWarning(logsshsftp) << "ERROR : SSH error " << rc;
                    fclose(tempstorage);
                    return false;
                }
            }
        }
    } while (!sftpfile);

    emit xfer();
    do {
        do {
            /* read in a loop until we block */
            xfer();
            rc = libssh2_sftp_read(sftpfile, buffer.data(), buffer.size());

            if(rc > 0) {
                /* write to temporary storage area */
                fwrite(buffer.data(), rc, 1, tempstorage);
            }
        } while (rc > 0);

        if(rc != LIBSSH2_ERROR_EAGAIN) {
            /* error or end of file */
            break;
        }

        m_waitData(1000);
    } while (true);

    libssh2_sftp_close(sftpfile);

    fclose(tempstorage);

    /* Remove file if is the same that original */
    if(dest != original)
    {
        QCryptographicHash hash1( QCryptographicHash::Md5 );
        QCryptographicHash hash2( QCryptographicHash::Md5 );
        QFile f1( original );
        if ( f1.open( QIODevice::ReadOnly ) ) {
            hash1.addData( f1.readAll() );
        }
        QByteArray sig1 = hash1.result();
        QFile f2( dest );
        if ( f2.open( QIODevice::ReadOnly ) ) {
            hash2.addData( f2.readAll() );
        }
        QByteArray sig2 = hash2.result();
        if(sig1 == sig2)
        {
            f2.remove();
        }
    }
    return true;
}

int SshSFtp::mkdir(const QString &dest)
{
    int res;

    while((res = libssh2_sftp_mkdir(m_sftpSession, qPrintable(dest), 0775)) == LIBSSH2_ERROR_EAGAIN)
    {
        m_waitData(2000);
    }

    if(res != 0)
    {
        qCWarning(logsshsftp) << "mkdir " << dest << " error, result = " << res;
    }
    else
    {
        qCDebug(logsshsftp) << "mkdir "<< dest << " OK";
    }

    return res;
}

QStringList SshSFtp::readdir(const QString &d)
{
    int rc;
    QStringList result;
    LIBSSH2_SFTP_HANDLE *sftpdir = getDirHandler(qPrintable(d));
    QByteArray buffer(512,0);

    do {
        LIBSSH2_SFTP_ATTRIBUTES attrs;

        /* loop until we fail */
        while ((rc = libssh2_sftp_readdir(sftpdir, buffer.data(), buffer.size(), &attrs)) == LIBSSH2_ERROR_EAGAIN)
        {
            m_waitData(2000);
        }
        if(rc > 0) {
            result.append(QString(buffer));
        }
        else if (rc != LIBSSH2_ERROR_EAGAIN) {
            break;
        }

    } while (true);
    closeDirHandler(qPrintable(d));
    return result;
}

bool SshSFtp::isDir(const QString &d)
{
    LIBSSH2_SFTP_HANDLE *sftpdir = getDirHandler(qPrintable(d));
    return sftpdir != nullptr;
}

bool SshSFtp::isFile(const QString &d)
{
    LIBSSH2_SFTP_ATTRIBUTES fileinfo;
    int status = LIBSSH2_ERROR_EAGAIN;
    while(status == LIBSSH2_ERROR_EAGAIN)
    {
        status = libssh2_sftp_stat(m_sftpSession,qPrintable(d),&fileinfo);
        if(status == LIBSSH2_ERROR_EAGAIN) m_waitData(2000);
    }
    qCDebug(logsshsftp) << "isFile(" << d << ") = " << status;
    return (status == 0);
}

int SshSFtp::mkpath(const QString &dest)
{
    qCDebug(logsshsftp) << "mkpath " << dest;
    if(isDir(dest)) return true;
    QStringList d = dest.split("/");
    d.pop_back();
    if(mkpath(d.join("/")))
    {
        mkdir(dest);
    }
    if(isDir(dest)) return true;
    return false;
}

bool SshSFtp::unlink(const QString &d)
{
    int res;

    while((res = libssh2_sftp_unlink(m_sftpSession, qPrintable(d))) == LIBSSH2_ERROR_EAGAIN)
    {
        m_waitData(2000);
    }

    if(res != 0)
    {
        qCWarning(logsshsftp) << "unlink " << d << " error, result = " << res;
    }
    else
    {
        qCDebug(logsshsftp) << "unlink "<< d << " OK";
    }
    return res;
}

quint64 SshSFtp::filesize(const QString &d)
{
    return getFileInfo(d).filesize;
}

void SshSFtp::sshDataReceived()
{
    emit sshData();
}

bool SshSFtp::m_waitData(int timeout)
{
    bool ret;
    QEventLoop datawait;
    QTimer timer;
    QObject::connect(this, SIGNAL(sshData()), &datawait, SLOT(quit()));
    QObject::connect(&timer, SIGNAL(timeout()), &datawait, SLOT(quit()));
    timer.setInterval(timeout);
    timer.start();
    datawait.exec();
    ret = timer.isActive();
    timer.stop();
    return ret;
}

LIBSSH2_SFTP_HANDLE *SshSFtp::getDirHandler(const QString &path)
{
    int rc;
    if(!m_dirhandler.contains(path))
    {
        LIBSSH2_SFTP_HANDLE *sftpdir = nullptr;
        do {
            sftpdir = libssh2_sftp_opendir(m_sftpSession, qPrintable(path));
            rc = libssh2_session_last_errno(m_sshClient->session());
            if (!sftpdir && (rc == LIBSSH2_ERROR_EAGAIN))
            {
                m_waitData(2000);
            }
            else if(!sftpdir && (rc == LIBSSH2_ERROR_SFTP_PROTOCOL))
            {
                    return nullptr;
            }
        } while (!sftpdir);
        m_dirhandler[path] = sftpdir;
    }
    return m_dirhandler[path];
}

LIBSSH2_SFTP_HANDLE *SshSFtp::closeDirHandler(const QString &path)
{
    if(m_dirhandler.contains(path))
    {
        LIBSSH2_SFTP_HANDLE *sftpdir = m_dirhandler[path];
        libssh2_sftp_closedir(sftpdir);
        m_dirhandler.remove(path);
    }
    return nullptr;
}

LIBSSH2_SFTP_ATTRIBUTES SshSFtp::getFileInfo(const QString &path)
{
    if(!m_fileinfo.contains(path))
    {
        LIBSSH2_SFTP_ATTRIBUTES fileinfo;
        int status = LIBSSH2_ERROR_EAGAIN;
        while(status == LIBSSH2_ERROR_EAGAIN)
        {
            status = libssh2_sftp_stat(m_sftpSession,qPrintable(path),&fileinfo);
            if(status == LIBSSH2_ERROR_EAGAIN) m_waitData(2000);
            else {
                m_fileinfo[path] = fileinfo;
            }
        }
    }
    return m_fileinfo[path];
}


