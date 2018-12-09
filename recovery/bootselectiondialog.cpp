/* 启动选择菜单
 *
 * 第一作者：Floris Bos
 * 由Raspberry Pi维护
 *
 * 有关许可证详细信息，请参阅LICENSE.txt
 *
 */

#include "bootselectiondialog.h"
#include "ui_bootselectiondialog.h"
#include "config.h"
#include "json.h"
#include "util.h"
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTimer>
#include <QSettings>
#include <QDesktopWidget>
#include <QScreen>
#include <QWSServer>
#include <QRegExp>
#include <QDebug>

BootSelectionDialog::BootSelectionDialog(const QString &drive, const QString &defaultPartition, QWidget *parent) :
    QDialog(parent),
    _countdown(11),
    ui(new Ui::BootSelectionDialog)
{
    setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    ui->setupUi(this);
    QRect s = QApplication::desktop()->screenGeometry();
    if (s.height() < 500)
        resize(s.width()-10, s.height()-100);

    QDir dir;
    dir.mkdir("/settings");

    if (QProcess::execute("mount -o remount,ro /settings") != 0
        && QProcess::execute("mount -t ext4 -o ro "+partdev(drive, SETTINGS_PARTNR)+" /settings") != 0)
    {
        QMessageBox::critical(this, tr("无法显示启动菜单"), tr("挂载设置分区时出错"));
        return;
    }

    /* 还安装recovery分区，因为它可能包含我们需要的图标 */
    if (QProcess::execute("mount -t vfat -o ro "+partdev(drive, 1)+" /mnt") != 0)
    {
        /* 如果失败则不致命 */
    }

    QVariantList installed_os = Json::loadFromFile("/settings/installed_os.json").toList();
    QSize currentsize = ui->list->iconSize();

    foreach (QVariant v, installed_os)
    {
        QVariantMap m = v.toMap();
        QString iconfilename = m.value("icon").toString();
        QIcon icon;

        if (!iconfilename.isEmpty() && QFile::exists(iconfilename))
        {
            icon = QIcon(iconfilename);
            QList<QSize> avs = icon.availableSizes();
            if (avs.isEmpty())
            {
                /* 图标文件损坏 */
                icon = QIcon();
            }
            else
            {
                QSize iconsize = avs.first();

                if (iconsize.width() > currentsize.width() || iconsize.height() > currentsize.height())
                {
                    /* 使所有图标与我们拥有的最大图标一样大 */
                    currentsize = QSize(qMax(iconsize.width(), currentsize.width()),qMax(iconsize.height(), currentsize.height()));
                    ui->list->setIconSize(currentsize);
                }
            }
        }
        if (canBootOs(m.value("name").toString(), m))
        {
            QListWidgetItem *item = new QListWidgetItem(icon, m.value("name").toString()+"\n"+m.value("description").toString(), ui->list);
            item->setData(Qt::UserRole, m);
        }
    }

    if (ui->list->count() != 0)
    {
        // 如果默认启动分区设置，则在5秒后启动到该启动分区
        QSettings settings("/settings/noobs.conf", QSettings::IniFormat, this);
        int partition = settings.value("default_partition_to_boot", defaultPartition).toInt();

        if (partition != 800)
        {
            // 启动计时器
            qDebug() << "在启动分区之前启动10秒计时器" << partition;
            _timer.setInterval(1000);
            connect(&_timer, SIGNAL(timeout()), this, SLOT(countdown()));
            _timer.start();
            countdown();
            ui->list->installEventFilter(this);

            // 选择之前启动的操作系统
            QString partnrStr = QString::number(partition);
            QRegExp partnrRx("([0-9]+)$");
            for (int i=0; i<ui->list->count(); i++)
            {
                QVariantMap m = ui->list->item(i)->data(Qt::UserRole).toMap();
                QString bootpart = m.value("partitions").toList().first().toString();
                if (partnrRx.indexIn(bootpart) != -1 && partnrRx.cap(1) == partnrStr)
                {
                    qDebug() << "以前的操作系统" << bootpart;
                    ui->list->setCurrentRow(i);
                    break;
                }
            }
        }
        else
        {
            ui->list->setCurrentRow(0);
        }
    }
    if (ui->list->count() == 1)
    {
        // 只有一个操作系统，启动它
        qDebug() << "accepting";
        QTimer::singleShot(1, this, SLOT(accept()));
    }

}

BootSelectionDialog::~BootSelectionDialog()
{
    delete ui;
}

void BootSelectionDialog::bootPartition()
{
    QSettings settings("/settings/noobs.conf", QSettings::IniFormat, this);
    QByteArray partition = settings.value("default_partition_to_boot", 800).toByteArray();
    qDebug() << "正在引导分区" << partition;
    setRebootPartition(partition);
    QDialog::accept();
}

void BootSelectionDialog::accept()
{
    QListWidgetItem *item = ui->list->currentItem();
    if (!item)
        return;

    QSettings settings("/settings/noobs.conf", QSettings::IniFormat, this);
    QVariantMap m = item->data(Qt::UserRole).toMap();
    QByteArray partition = m.value("partitions").toList().first().toByteArray();

    int partitionNr;
    QRegExp parttype("^PARTUUID");
    if (parttype.indexIn(partition) == -1)
    {
        // SD卡样式 /dev/mmcblk0pDD
        QRegExp partnrRx("([0-9]+)$");
        if (partnrRx.indexIn(partition) == -1)
        {
            QMessageBox::critical(this, "installed_os.json corrupt", "Not a valid partition: "+partition);
            return;
        }
        partitionNr    = partnrRx.cap(1).toInt();
    }
    else
    {
        // USB样式 PARTUUID=000dbedf-XX
        QRegExp partnrRx("([0-9a-f][0-9a-f])$");
        if (partnrRx.indexIn(partition) == -1)
        {
            QMessageBox::critical(this, "installed_os.json corrupt", "不是有效的分区："+partition);
            return;
        }
        bool ok;
        partitionNr    = partnrRx.cap(1).toInt(&ok, 16);
    }

    int oldpartitionNr = settings.value("default_partition_to_boot", 0).toInt();

    if (partitionNr != oldpartitionNr)
    {
        // 将操作系统启动选项保存为新的默认值
        QProcess::execute("mount -o remount,rw /settings");
        settings.setValue("default_partition_to_boot", partitionNr);
        settings.sync();
        QProcess::execute("mount -o remount,ro /settings");
    }

    bootPartition();
}

void BootSelectionDialog::on_list_activated(const QModelIndex &)
{
    accept();
}

void BootSelectionDialog::setDisplayMode()
{
#ifdef Q_WS_QWS
    QString cmd, mode;
    QSettings settings("/settings/noobs.conf", QSettings::IniFormat, this);

    /* 恢复已保存的显示模式 */
    int modenr = settings.value("display_mode", 0).toInt();

    switch (modenr)
    {
    case 1:
        cmd  = "-e \'DMT 4 DVI\'";
        mode = tr("HDMI安全模式");
        break;
    case 2:
        cmd  = "-c \'PAL 4:3\'";
        mode = tr("复合PAL模式");
        break;
    case 3:
        cmd  = "-c \'NTSC 4:3\'";
        mode = tr("复合NTSC模式");
        break;

    default:
        return;
    }

    // 触发帧缓冲区调整大小
    QProcess *presize = new QProcess(this);
    presize->start(QString("sh -c \"tvservice -o; tvservice %1;\"").arg(cmd));
    presize->waitForFinished(4000);

    // 使用当前值更新屏幕分辨率（即使我们没有
    // 得到我们认为我们得到的）
    QProcess *update = new QProcess(this);
    update->start(QString("sh -c \"tvservice -s | cut -d , -f 2 | cut -d \' \' -f 2 | cut -d x -f 1;tvservice -s | cut -d , -f 2 | cut -d \' \' -f 2 | cut -d x -f 2\""));
    update->waitForFinished(4000);
    update->setProcessChannelMode(QProcess::MergedChannels);

    QTextStream stream(update);
    int xres = stream.readLine().toInt();
    int yres = stream.readLine().toInt();
    int oTop = 0, oBottom = 0, oLeft = 0, oRight = 0;
    getOverscan(oTop, oBottom, oLeft, oRight);
    QScreen::instance()->setMode(xres-oLeft-oRight, yres-oTop-oBottom, 16);

    // 更新UI项目位置
    QRect s = QApplication::desktop()->screenGeometry();
    if (s.height() < 400)
        resize(s.width()-10, s.height()-100);
    setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), qApp->desktop()->availableGeometry()));

    // 刷新屏幕
    qApp->processEvents();
    QWSServer::instance()->refresh();
#endif
}

bool BootSelectionDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress)
    {
        stopCountdown();
    }

    return QDialog::eventFilter(obj, event);
}

void BootSelectionDialog::stopCountdown()
{
    _timer.stop();
    setWindowTitle(tr("选择要启动的操作系统"));
}

void BootSelectionDialog::countdown()
{
    setWindowTitle(tr("之前选择的操作系统将在％1秒内启动").arg(--_countdown));
    if (_countdown == 0)
    {
        _timer.stop();
        bootPartition();
    }
}
