#ifndef TERMINAL_H
#define TERMINAL_H

#include <QQuickPaintedItem>
#include <QVariantList>
#include <QSocketNotifier>
#include <QTimer>
#include <QElapsedTimer>
#include <vector>
#include <string>
#include <vterm.h>

class Terminal : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString fontName READ fontName WRITE setFontName NOTIFY fontNameChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)
    Q_PROPERTY(bool terminalFocused READ terminalFocused WRITE setTerminalFocused NOTIFY terminalFocusedChanged)
    Q_PROPERTY(bool useAltScreen READ useAltScreen NOTIFY useAltScreenChanged)
    Q_PROPERTY(int scrollOffset READ scrollOffset WRITE setScrollOffset NOTIFY scrollOffsetChanged)
    Q_PROPERTY(int historySize READ historySize NOTIFY historySizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY rowsChanged)

public:
    explicit Terminal(QQuickItem *parent = nullptr);
    ~Terminal();

    QString fontName() const { return m_fontName; }
    void setFontName(const QString &name);

    int fontSize() const { return m_fontSize; }
    void setFontSize(int size);

    bool isConnected() const { return m_isConnected; }

    bool terminalFocused() const { return m_terminalFocused; }
    void setTerminalFocused(bool focused);

    bool useAltScreen() const { return m_useAltScreen; }

    int scrollOffset() const { return m_scrollOffset; }
    void setScrollOffset(int offset);

    int historySize() const { return static_cast<int>(m_history.size()); }
    int rows() const { return m_rows; }

    void paint(QPainter *painter) override;

    Q_INVOKABLE void sendInput(const QString &input);
    Q_INVOKABLE void setTheme(const QString &bgHex, const QString &fgHex, bool isDark);
    Q_INVOKABLE void scrollLines(int lines);
    Q_INVOKABLE bool isAltScreen() const { return m_useAltScreen; }
    Q_INVOKABLE void recreateShell();
    Q_INVOKABLE void sendMouseEvent(int type, int button, int x, int y, bool isPressed);
    Q_INVOKABLE bool isMouseTrackingActive() const { return m_mouseTrackingClick || m_mouseTrackingDrag; }
    Q_INVOKABLE void startSelection(double x, double y);
    Q_INVOKABLE void updateSelection(double x, double y);
    Q_INVOKABLE void endSelection();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool hasSelection() const { return m_hasSelection; }
    Q_INVOKABLE QString selectedText() const;
    Q_INVOKABLE void pasteFromSelection();
    Q_INVOKABLE void pasteFromClipboard();
    Q_INVOKABLE bool isPositionHoverable(double x, double y) const;
    Q_INVOKABLE void selectWord(double x, double y);
    Q_INVOKABLE void selectLine(double x, double y);

    void writeToPty(const char *s, size_t len);

signals:
    void fontNameChanged();
    void fontSizeChanged();
    void isConnectedChanged();
    void terminalFocusedChanged();
    void useAltScreenChanged();
    void scrollOffsetChanged();
    void historySizeChanged();
    void rowsChanged();
    void bell();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private slots:
    void onPtyDataAvailable();

private:
    struct SelectionPoint {
        int col;
        int row;
    };

    SelectionPoint cellAt(double x, double y) const;
    bool isCellSelected(int col, int row) const;
    int getLineLength(int row) const;
    bool getCellAt(int r, int c, VTermScreenCell *cell) const;
    QString getSelectedText() const;

    void initPty();
    void initVTerm();
    void freeVTerm();
    
    QString getColorHex(const VTermColor &color);
    void sendResize(int cols, int rows);
    void recalculateGridSize();

    // VTerm Callback bridges
    static int cb_damage(VTermRect rect, void *user);
    static int cb_moverect(VTermRect dest, VTermRect src, void *user);
    static int cb_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int cb_settermprop(VTermProp prop, VTermValue *val, void *user);
    static int cb_bell(void *user);
    static int cb_resize(int rows, int cols, void *user);
    static int cb_sb_pushline(int cols, const VTermScreenCell *cells, void *user);
    static int cb_sb_popline(int cols, VTermScreenCell *cells, void *user);
    static int cb_sb_clear(void *user);

    int m_cols = 80;
    int m_rows = 24;
    int m_ptmxFd = -1;
    pid_t m_ptyPid = -1;
    QSocketNotifier *m_notifier = nullptr;

    // libvterm objects
    VTerm *m_vt = nullptr;
    VTermScreen *m_vts = nullptr;

    // Scrollback history
    std::vector<std::vector<VTermScreenCell>> m_history;
    int m_scrollOffset = 0;
    
    // Position of cursor
    int m_cx = 0;
    int m_cy = 0;

    // Selection state
    bool m_hasSelection = false;
    bool m_isSelecting = false;
    SelectionPoint m_selectionStart{0, 0};
    SelectionPoint m_selectionEnd{0, 0};
    QElapsedTimer m_clickTimer;
    int m_clickCount = 0;

    QString m_colorMap[16];
    
    bool m_isConnected = false;
    bool m_shellExited = false;
    bool m_terminalFocused = false;

    QString m_fontName = "Monospace";
    int m_fontSize = 13;

    // Properties populated via settermprop callbacks
    bool m_cursorVisible = true;
    bool m_useAltScreen = false;
    bool m_mouseTrackingClick = false;
    bool m_mouseTrackingDrag = false;
    bool m_mouseSgrMode = false;
};

#endif // TERMINAL_H

