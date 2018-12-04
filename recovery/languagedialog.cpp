/* 语言选择对话框
 *
 * 第一作者：Floris Bos
 * 由Raspberry Pi维护
 *
 * 有关许可证详细信息，请参阅LICENSE.txt
 *
 */

#include "languagedialog.h"
#include "ui_languagedialog.h"
#include "config.h"
#include <QIcon>
#include <QDebug>
#include <QFile>
#include <QTranslator>
#include <QDir>
#include <QLocale>
#include <QKeyEvent>
#include <QWSServer>
#include <QKbdDriverFactory>
#include <QProcess>
#include <QSettings>

/* 用于更新的额外字符串，用于检测并移交给翻译人员进行翻译 */
#if 0
QT_TRANSLATE_NOOP("QDialogButtonBox","确定")
QT_TRANSLATE_NOOP("QDialogButtonBox","&确定")
QT_TRANSLATE_NOOP("QDialogButtonBox","取消")
QT_TRANSLATE_NOOP("QDialogButtonBox","&取消")
QT_TRANSLATE_NOOP("QDialogButtonBox","关闭")
QT_TRANSLATE_NOOP("QDialogButtonBox","&关闭")
QT_TRANSLATE_NOOP("QDialogButtonBox","&是")
QT_TRANSLATE_NOOP("QDialogButtonBox","&否")
#endif

LanguageDialog *LanguageDialog::_instance = NULL;

LanguageDialog::LanguageDialog(const QString &defaultLang, const QString &defaultKeyboard, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LanguageDialog),
    _trans(NULL), _qttrans(NULL)
{
    _instance = this;

    setAttribute(Qt::WA_ShowWithoutActivating);

    qDebug() << "默认语言是" << defaultLang;
    qDebug() << "默认键盘布局是" << defaultKeyboard;

    QSettings settings("/settings/noobs.conf", QSettings::IniFormat, this);
    QString savedLang = settings.value("language", defaultLang).toString();
    QString savedKeyLayout = settings.value("keyboard_layout", defaultKeyboard).toString();

    ui->setupUi(this);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_QuitOnClose, false);

    QDir kdir("/keymaps/", "*.qmap");
    QStringList keyboardlayouts = kdir.entryList();
    foreach (QString layoutfile, keyboardlayouts)
    {
        layoutfile.chop(5);
        ui->keyCombo->addItem(layoutfile, layoutfile);
    }

    ui->langCombo->addItem(QIcon(":/icons/gb.png"), "English (UK)", "gb");
    ui->langCombo->addItem(QIcon(":/icons/us.png"), "English (US)", "us");

    /* 搜索翻译资源文件 */
    QDir dir(":/", "translation_*.qm");
    QStringList translations = dir.entryList();

    foreach (QString langfile, translations)
    {
        QString langcode = langfile.mid(12);
        langcode.chop(3);
        QLocale loc(langcode);
        /* 用英语显示语言名称，例如 德语，法语 */
        /* QString languagename = QLocale::languageToString(loc.language()); */
        /* 应以母语显示语言名称，例如 Deutsch，Français  */
        QString languagename = loc.nativeLanguageName();

        /* 阿斯图里亚斯的例外情况（不在ISO 639-1中） */
        if (langcode.compare("ast", Qt::CaseInsensitive) == 0)
            languagename = "Asturian";

        QString iconfilename = ":/icons/"+langcode+".png";

        if (QFile::exists(iconfilename))
            ui->langCombo->addItem(QIcon(iconfilename), languagename, langcode);
        else
            ui->langCombo->addItem(languagename, langcode);

        if (langcode.compare(savedLang, Qt::CaseInsensitive) == 0)
        {
            _currentLang = langcode;
            ui->langCombo->setCurrentIndex(ui->langCombo->count() - 1);
        }
    }

    changeLanguage(savedLang);
    changeKeyboardLayout(savedKeyLayout);
    ui->keyCombo->setCurrentIndex(ui->keyCombo->findData(savedKeyLayout));
}

LanguageDialog::~LanguageDialog()
{
    delete ui;
}

void LanguageDialog::changeKeyboardLayout(const QString &langcode)
{
#ifdef Q_WS_QWS
    QString keymapfile = QString("/keymaps/%1.qmap").arg(langcode);

    if (QFile::exists(keymapfile))
    {
        QWSServer *q = QWSServer::instance();
        q->closeKeyboard();
        q->setKeyboardHandler(QKbdDriverFactory::create("TTY", "keymap="+keymapfile));
    }
#else
    Q_UNUSED(langcode)
#endif

        // 将新语言选择保存到INI文件
        QSettings settings("/settings/noobs.conf", QSettings::IniFormat, this);
        settings.setValue("keyboard_layout", langcode);
        settings.sync();
}

void LanguageDialog::changeLanguage(const QString &langcode)
{
    //if (langcode == _currentLang)
    //    return;

    if (_trans)
    {
        QApplication::removeTranslator(_trans);
        delete _trans;
        _trans = NULL;
    }
    if (_qttrans)
    {
        QApplication::removeTranslator(_qttrans);

        delete _qttrans;
        _qttrans = NULL;
    }

    if (!(langcode == "us" || langcode == "gb"))
    {
        /* qt_<languagecode>.qm are generic language translation files provided by the Qt team
         * this can translate common things like the "OK" and "Cancel" button of dialog boxes
         * 不幸的是，它们不适用于所有语言，但如果我们有一种语言，则使用一种语言。 */
        if ( QFile::exists(":/qt_"+langcode+".qm" ))
        {
            _qttrans = new QTranslator();
            _qttrans->load(":/qt_" + langcode+".qm");
            QApplication::installTranslator(_qttrans);
        }

        /* the translation_<languagecode>.qm file is specific to our application */
        if ( QFile::exists(":/translation_"+langcode+".qm"))
        {
            _trans = new QTranslator();
            _trans->load(":/translation_"+langcode+".qm");
            QApplication::installTranslator(_trans);
        }
    }

    /* 更新键盘布局 */
    QString defaultKeyboardLayout;
    if (langcode == "nl")
    {
        /* In some countries US keyboard layout is more predominant, although they
         * also do have there own keyboard layout for historic reasons */
        defaultKeyboardLayout = "us";
    }
    else if (langcode == "ja")
    {
        defaultKeyboardLayout = "jp";
    }
    else if (langcode == "sv")
    {
        defaultKeyboardLayout = "se";
    }
    else
    {
        defaultKeyboardLayout = langcode;
    }
    int idx = ui->keyCombo->findData(defaultKeyboardLayout);

    if (idx == -1)
    {
        /* 如果没有该语言的键盘布局，则默认为美国键盘布局 */
        idx = ui->keyCombo->findData("us");
    }
    if (idx != -1)
    {
        ui->keyCombo->setCurrentIndex(idx);
    }

    _currentLang = langcode;

    // 将新语言选择保存到INI文件
    QSettings settings("/settings/noobs.conf", QSettings::IniFormat, this);
    settings.setValue("language", langcode);
    settings.sync();
}

void LanguageDialog::on_langCombo_currentIndexChanged(int index)
{
    QString langcode = ui->langCombo->itemData(index).toString();

    changeLanguage(langcode);
}

void LanguageDialog::changeEvent(QEvent* event)
{
    if (event && event->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);

    QDialog::changeEvent(event);
}

void LanguageDialog::on_actionOpenComboBox_triggered()
{
    ui->langCombo->showPopup();
}

void LanguageDialog::on_actionOpenKeyCombo_triggered()
{
    ui->keyCombo->showPopup();
}

void LanguageDialog::on_keyCombo_currentIndexChanged(int index)
{
    QString keycode = ui->keyCombo->itemData(index).toString();

    changeKeyboardLayout(keycode);
}

LanguageDialog *LanguageDialog::instance(const QString &defaultLang, const QString &defaultKeyboard)
{
    /* Singleton */
    if (!_instance)
        new LanguageDialog(defaultLang, defaultKeyboard);

    return _instance;
}

QString LanguageDialog::currentLanguage()
{
    return _currentLang;
}
