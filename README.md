# QtSsh
Qt Library Wrapper to libssh2

* This Project need to be included in a larger project with gitmodule
* You just need to add include(QtSsh/QtSsh.pri) in your .pro, and to include/link with libssh2

Here is Example code：  

#include <QCoreApplication>
#include "sshsftp.h"

#include "sshclient.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);


    SshClient *sshClient = new SshClient("SFTPClient");

    // 设置代理和连接超时时间
    sshClient->setProxy(nullptr);
    sshClient->setConnectTimeout(5000);

    // 连接到 SFTP 服务器
    QString host = "127.0.0.1";
    QString user = "root";
    QString password = "root";
    QByteArrayList methods = {"password"}; // 指定认证方法为密码认证
    int port = 2222;  // SFTP 默认端口为 22

    sshClient->setPassphrase("root");
    int connResult = sshClient->connectToHost(user, host, port, methods, 10000);
    if (connResult != 0) {
        qDebug() << "连接失败：" << connResult;
    } else {
        qDebug() << "连接成功！";
    }

    // 等待连接状态变为 Ready
    if (sshClient->waitForState(SshClient::SshState::Ready)) {
        qDebug() << "连接已就绪，可以开始进行文件传输、命令执行等操作。";
    } else {
        qDebug() << "连接超时或失败。";
    }

    // 创建 SFTP 对象
    SshSFtp *sftp = new SshSFtp("MySftpClient", sshClient);


    // 上传文件
    QString localFilePath = "C:/Users/Vdeilae/Desktop/1.xlsx";
    QString remoteFilePath = "/";
    QString result = sftp->send(localFilePath, remoteFilePath);
    if (result.isEmpty()) {
        qDebug() << "上传文件失败：" << sftp->errMsg().join("\n");
    } else {
        qDebug() << "上传文件成功：" << result;
    }

    sftp->mkdir("/new1");

    bool ifexist=sftp->isFile("/1.xlsx");
    if(ifexist)
    {
        qDebug()<<"exist";
    }
    else{
        qDebug()<<"NO Exist";
    }


    // 断开连接
    sftp->deleteLater();
    sshClient->disconnectFromHost();
    return a.exec();
}

