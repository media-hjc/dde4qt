#pragma once

#include <QCoreApplication>
#include <QAbstractNativeEventFilter>
#include <QSettings>
#include <QUrl>
#include <QStringList>

#define WIN32_LEAN_AND_MEAN
#include <wtypes.h>
#include <winnt.h>
#include <winbase.h>
#include <winuser.h>
#include <dde.h>

namespace win32 {

    /**
    ** @brief The Atom class WIN32系统全局原子表操作类 将传入的参数放入原子表中
    **        系统提供许多原子表.每个原子表用于不同的目的;
    **        例如,动态数据交换(DDE)应用程序使用全局原子表(global atom table)
    **        与其他应用程序共享项目名称和主题名称字符串.不用传递实际的字符串,
    **        一个DDE应用程序传递全局原子给它的父进程.父进程使用原子提取原子表中的字符串等等
    **/
    class Atom {
    public:
        Atom(const QString& name) : atom(GlobalAddAtomW((PCWSTR)name.utf16())) {
        }

        ~Atom() {
            ::GlobalDeleteAtom(atom);
        }

        operator ATOM() const { return atom; }
    private:
        ATOM atom;
    };
    /**
    * @brief The QDdeFilter class DDE事件处理类,连接 注销 信息收发 将程序名和主题放入原子表中
    *        目前只能处理连接 注销 命令执行。  信息传递功能有待开发
    */
    class QDdeFilter : public QObject, public QAbstractNativeEventFilter
    {
        Q_OBJECT    //宏Q_OBJECT。在Qt的程序中如果使用了信号与反应槽就必须在类的定义中声明这个宏，
                    //不过如果你声明了该宏但在程序中并没有信号与反应槽，对程序也不会有任何影响，
                    //所以建议大家在用Qt写程序时不妨都把这个宏加上。
        Q_SIGNALS:  //宏替换类声明中的signals关键字
            void command(const QString& command);

        public:
            //构造函数
            QDdeFilter(const QString& application, const QString& topic, QObject* parent=nullptr)
              : QObject(parent),application(application), topic(topic)
            {

            }

        private:
            //外部不可访问的事件处理函数
            bool nativeEventFilter(const QByteArray& /*eventType*/, void* message, long* /*result*/) override
            {
                auto msg = reinterpret_cast<LPMSG>(message);
                if (msg->message == WM_DDE_INITIATE)  //收到此消息将回复一个WM_DDE_ACK类型消息以启动与客户端程序的对话
                {
                    if (LOWORD(msg->lParam) == application && HIWORD(msg->lParam) == topic)
                    {
                        ::SendMessageW((HWND)msg->wParam, WM_DDE_ACK, (WPARAM)msg->hwnd, ::ReuseDDElParam(msg->lParam, WM_DDE_INITIATE, WM_DDE_ACK, application, topic));
                        return true;
                    }
                }
                else if (msg->message == WM_DDE_EXECUTE)//接收到此类型信息将把收到的字符串作为命令进行处理
                {
                    HGLOBAL hcommand;
                    //解压缩从已发布的DDE消息中收到的动态数据交换（DDE）lParam值 将值的高位指针传给hcommand
                    ::UnpackDDElParam(msg->message, msg->lParam, 0, (PUINT_PTR)&hcommand);
                    //将hcommand先锁定再处理后传值给commandString
                    const QString commandString(QString::fromUtf16((const ushort*)::GlobalLock((HGLOBAL)hcommand)));
                    //处理完成后解锁hcommand
                    ::GlobalUnlock(hcommand);
                    //回复一个WM_DDE_ACK消息给客户端
                    ::PostMessageW((HWND)msg->wParam, WM_DDE_ACK, (WPARAM)msg->hwnd, ::ReuseDDElParam(msg->lParam, WM_DDE_EXECUTE, WM_DDE_ACK, (UINT)0x8000, (UINT_PTR)hcommand));
                    emit command(commandString);  //向外发出信号command(commandString)  由connect()定义的反应槽函数接收并执行commandString表示的操作
                    return true;
                }
                else if (msg->message == WM_DDE_TERMINATE) //收到此消息代表需要终止对话
                {
                    ::PostMessageW((HWND)msg->wParam, WM_DDE_TERMINATE, (WPARAM)msg->hwnd, 0);
                }
                return false;
            }

        Atom application;
        Atom topic;    //实例化application  topic两个对象
    };
    /**
     * @brief The QUrlProtocolHandler class  路径协议处理程序类
     */
    class QUrlProtocolHandler : public QObject
    {
        Q_OBJECT
        Q_SIGNALS:  //发射信号函数
            void activate(const QUrl& url);

        public:
            //警告提示warning: ‘xxx’ will be initialized after 'xxx'
            //其实是由于我们在初始化成员变量的时候没有按照成员声明的顺序初始化造成的，
            //所以以后在使用Qt进行开发应用程序时，应该按照头文件中成员变量声明的顺序
            //进行初始化就不会出现上述的警告了  无视该警告 程序仍然可以正常编译

            //
            QUrlProtocolHandler(const QString& schema, const QString& application = QCoreApplication::applicationName(), const QString& topic = QStringLiteral("System"), QObject* parent=nullptr) :
              QObject(parent),
              schema(schema),
              application(application),
              topic(topic),
              ddeFilter(application, topic, this)
            {
                connect(&ddeFilter, &QDdeFilter::command, this, &QUrlProtocolHandler::onCommand);
                QCoreApplication::instance()->installNativeEventFilter(&ddeFilter);
            }

            ~QUrlProtocolHandler()
            {
                QCoreApplication::instance()->removeNativeEventFilter(&ddeFilter);
            }
            //将程序运行参数列表中的第一个(包含完整路径的程序名称) 作为参数传入 将以下几种值写入注册表中  uninstall()时会自动删除掉
            void install(const QString& applicationPath = QCoreApplication::instance()->arguments().at(0))
            {
                if (!schema.isEmpty())
                {
                    //QSettings实例化一个registry对象 选择合适的储存格式保存信息  在Windows上，这意味着系统注册表。
                    //在macOS和iOS上，这意味着CFPreferences API； 在Unix上，这意味着INI格式的文本配置文件。
                    //将schema格式化输出到Class注册表键的下面建立一个dde4qt目录
                    QSettings registry(QString("HKEY_CURRENT_USER\\SOFTWARE\\Classes\\%1").arg(schema), QSettings::NativeFormat);
                    registry.setValue("URL Protocol", "");                          //在上级目录下建立URL Protocol键  值为空
                    registry.setValue("shell/open/command/.", applicationPath);     //将作为参数传入的applicationPath作为值写入上级目录下的shell/open/command/. (默认值)
                    registry.setValue("shell/open/ddeexec/.", "%1");                //上级目录下写入shell/open/ddeexec/. 值为%1
                    registry.setValue("shell/open/ddeexec/application/.", application);  //写入程序名
                    registry.setValue("shell/open/ddeexec/topic/.", topic);         //写入主题
                    registry.sync();   //保存到注册表
                }
            }
            //删除注册表中留下的信息
            void uninstall()
            {
                // Prevent catastrophic removal of all Classes subkey
                if (!schema.isEmpty())
                {
                    QSettings registry(QString("HKEY_CURRENT_USER\\SOFTWARE\\Classes"), QSettings::NativeFormat);  //定位到注册表Classes项
                    registry.remove(schema);   //删除注册表项schema  值为dde4qt
                }
            }

        private Q_SLOTS:  //槽函数
            void onCommand(const QString& command)
            {
                emit activate(QUrl(command));
            }

        private:
            const QString schema;
            const QString application;
            const QString topic;
            QDdeFilter ddeFilter;
    };
} // win32
