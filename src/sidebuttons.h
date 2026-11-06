#pragma once

#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QWindow>

// The mouse's side buttons go back and forward, as in a browser. They are
// caught on the window before any item sees them: the page's scroll view
// would otherwise take the press and nothing would happen.
class SideButtons : public QObject {
public:
    explicit SideButtons(QWindow *window) : QObject(window), m_window(window) { window->installEventFilter(this); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        const QEvent::Type type = event->type();
        if (watched != m_window || (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease
                                    && type != QEvent::MouseButtonDblClick))
            return false;
        const Qt::MouseButton button = static_cast<QMouseEvent *>(event)->button();
        if (button != Qt::BackButton && button != Qt::ForwardButton)
            return false;
        // Act on the press; swallow the release and double click so nothing
        // else sees half of it.
        if (type == QEvent::MouseButtonPress)
            QMetaObject::invokeMethod(m_window, button == Qt::BackButton ? "back" : "forward");
        return true;
    }

private:
    QWindow *m_window;
};
