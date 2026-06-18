#include "terminal.h"
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <QPainter>
#include <QFontMetricsF>
#include <QDebug>
#include <QGuiApplication>
#include <QStyleHints>
#include <QClipboard>
#include <QChar>
#include <algorithm>

// Output callback to send data from vterm back to the shell process
static void cb_vterm_output(const char *s, size_t len, void *user) {
    static_cast<Terminal*>(user)->writeToPty(s, len);
}

void Terminal::writeToPty(const char *s, size_t len) {
    if (m_isConnected && m_ptmxFd >= 0) {
        ssize_t w = write(m_ptmxFd, s, len);
        Q_UNUSED(w);
    }
}

Terminal::Terminal(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setTheme("#1d1f21", "#eaeaea", true);
    m_cursorVisible = true;
    m_mouseTrackingClick = false;
    m_mouseTrackingDrag = false;
    m_mouseSgrMode = false;
    
    initVTerm();
    initPty();
}

Terminal::~Terminal() {
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
    }
    if (m_ptmxFd >= 0) {
        close(m_ptmxFd);
    }
    if (m_ptyPid >= 0) {
        kill(m_ptyPid, SIGKILL);
        waitpid(m_ptyPid, nullptr, WNOHANG);
    }
    freeVTerm();
}

void Terminal::initVTerm() {
    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    
    // Set output callback for keyboard/mouse responses
    vterm_output_set_callback(m_vt, cb_vterm_output, this);

    m_vts = vterm_obtain_screen(m_vt);
    vterm_screen_enable_altscreen(m_vts, 1);
    vterm_screen_set_damage_merge(m_vts, VTERM_DAMAGE_CELL);

    // Set screen callbacks
    static VTermScreenCallbacks cb = {
        /* damage */      cb_damage,
        /* moverect */    cb_moverect,
        /* movecursor */  cb_movecursor,
        /* settermprop */ cb_settermprop,
        /* bell */        cb_bell,
        /* resize */      cb_resize,
        /* sb_pushline */ cb_sb_pushline,
        /* sb_popline */  cb_sb_popline,
        /* sb_clear */    cb_sb_clear,
    };
    vterm_screen_set_callbacks(m_vts, &cb, this);
    vterm_screen_reset(m_vts, 1);
}

void Terminal::freeVTerm() {
    if (m_vt) {
        vterm_free(m_vt);
        m_vt = nullptr;
        m_vts = nullptr;
    }
}

int Terminal::cb_damage(VTermRect rect, void *user) {
    Q_UNUSED(rect);
    static_cast<Terminal*>(user)->update();
    return 1;
}

int Terminal::cb_moverect(VTermRect dest, VTermRect src, void *user) {
    Q_UNUSED(dest);
    Q_UNUSED(src);
    static_cast<Terminal*>(user)->update();
    return 1;
}

int Terminal::cb_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user) {
    Q_UNUSED(oldpos);
    Q_UNUSED(visible);
    Terminal *term = static_cast<Terminal*>(user);
    term->m_cx = pos.col;
    term->m_cy = pos.row;
    term->update();
    return 1;
}

int Terminal::cb_settermprop(VTermProp prop, VTermValue *val, void *user) {
    Terminal *term = static_cast<Terminal*>(user);
    switch (prop) {
        case VTERM_PROP_CURSORVISIBLE:
            term->m_cursorVisible = val->boolean;
            break;
        case VTERM_PROP_ALTSCREEN:
            term->m_useAltScreen = val->boolean;
            emit term->useAltScreenChanged();
            break;
        case VTERM_PROP_MOUSE:
            term->m_mouseTrackingClick = (val->number >= VTERM_PROP_MOUSE_CLICK);
            term->m_mouseTrackingDrag = (val->number >= VTERM_PROP_MOUSE_DRAG);
            break;
        default:
            break;
    }
    term->update();
    return 1;
}

int Terminal::cb_bell(void *user) {
    emit static_cast<Terminal*>(user)->bell();
    return 1;
}

int Terminal::cb_resize(int rows, int cols, void *user) {
    Terminal *term = static_cast<Terminal*>(user);
    term->m_rows = rows;
    term->m_cols = cols;
    return 1;
}

int Terminal::cb_sb_pushline(int cols, const VTermScreenCell *cells, void *user) {
    Terminal *term = static_cast<Terminal*>(user);
    std::vector<VTermScreenCell> line(cells, cells + cols);
    term->m_history.push_back(line);
    if (term->m_history.size() > 1000) {
        term->m_history.erase(term->m_history.begin());
    }
    return 1;
}

int Terminal::cb_sb_popline(int cols, VTermScreenCell *cells, void *user) {
    Terminal *term = static_cast<Terminal*>(user);
    if (term->m_history.empty()) return 0;
    auto line = term->m_history.back();
    term->m_history.pop_back();
    int limit = std::min(cols, (int)line.size());
    for (int i = 0; i < limit; ++i) {
        cells[i] = line[i];
    }
    for (int i = limit; i < cols; ++i) {
        cells[i].chars[0] = 0;
        cells[i].width = 1;
        cells[i].fg.type = VTERM_COLOR_DEFAULT_FG;
        cells[i].bg.type = VTERM_COLOR_DEFAULT_BG;
        cells[i].attrs = VTermScreenCellAttrs{};
    }
    return 1;
}

int Terminal::cb_sb_clear(void *user) {
    Terminal *term = static_cast<Terminal*>(user);
    term->m_history.clear();
    return 1;
}

void Terminal::setFontName(const QString &name) {
    if (m_fontName != name) {
        m_fontName = name;
        emit fontNameChanged();
        recalculateGridSize();
        update();
    }
}

void Terminal::setFontSize(int size) {
    if (m_fontSize != size) {
        m_fontSize = size;
        emit fontSizeChanged();
        recalculateGridSize();
        update();
    }
}

void Terminal::setTerminalFocused(bool focused) {
    if (m_terminalFocused != focused) {
        m_terminalFocused = focused;
        emit terminalFocusedChanged();
        m_cursorVisible = true;
        update();
    }
}

void Terminal::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    recalculateGridSize();
}

void Terminal::recalculateGridSize() {
    QFont font(m_fontName, m_fontSize);
    QFontMetricsF fm(font);
    double charWidth = fm.horizontalAdvance("X");
    double charHeight = fm.height();
    if (charWidth <= 0 || charHeight <= 0) return;

    int cols = std::max(40, static_cast<int>(width() / charWidth));
    int rows = std::max(15, static_cast<int>(height() / charHeight));
    
    sendResize(cols, rows);
}

void Terminal::initPty() {
    struct winsize ws;
    ws.ws_row = m_rows;
    ws.ws_col = m_cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    m_ptyPid = forkpty(&m_ptmxFd, nullptr, nullptr, &ws);
    if (m_ptyPid < 0) {
        qWarning() << "Failed to fork pty";
        return;
    }

    if (m_ptyPid == 0) {
        setenv("TERM", "xterm-256color", 1);
        setenv("LANG", "en_US.UTF-8", 1);

        const char *shell = getenv("SHELL");
        if (!shell) {
            shell = "/bin/bash";
        }
        execl(shell, shell, "-i", nullptr);
        _exit(1);
    }

    int flags = fcntl(m_ptmxFd, F_GETFL, 0);
    fcntl(m_ptmxFd, F_SETFL, flags | O_NONBLOCK);

    m_notifier = new QSocketNotifier(m_ptmxFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Terminal::onPtyDataAvailable);
    m_notifier->setEnabled(true);

    m_isConnected = true;
    emit isConnectedChanged();
}

void Terminal::onPtyDataAvailable() {
    char buf[4096];
    ssize_t n = read(m_ptmxFd, buf, sizeof(buf));
    if (n > 0) {
        m_scrollOffset = 0;
        if (m_vt) {
            vterm_input_write(m_vt, buf, n);
            vterm_screen_flush_damage(m_vts);
        }
        update();
    } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        m_notifier->setEnabled(false);
        m_isConnected = false;
        m_shellExited = true;
        emit isConnectedChanged();

        // Print exit notification
        const char *msg = "\r\n[Process completed. Press Enter to restart]";
        if (m_vt) {
            vterm_input_write(m_vt, msg, strlen(msg));
            vterm_screen_flush_damage(m_vts);
        }
        update();
    }
}

void Terminal::sendInput(const QString &input) {
    if (m_shellExited) {
        recreateShell();
        return;
    }
    m_scrollOffset = 0;
    if (m_ptmxFd >= 0) {
        QByteArray bytes = input.toUtf8();
        ssize_t w = write(m_ptmxFd, bytes.constData(), bytes.size());
        Q_UNUSED(w);
    }
}

void Terminal::recreateShell() {
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
    if (m_ptmxFd >= 0) {
        close(m_ptmxFd);
        m_ptmxFd = -1;
    }
    if (m_ptyPid >= 0) {
        kill(m_ptyPid, SIGKILL);
        waitpid(m_ptyPid, nullptr, WNOHANG);
        m_ptyPid = -1;
    }

    m_cx = 0;
    m_cy = 0;
    m_scrollOffset = 0;
    m_history.clear();
    m_useAltScreen = false;
    m_shellExited = false;
    m_mouseTrackingClick = false;
    m_mouseTrackingDrag = false;
    m_mouseSgrMode = false;
    emit useAltScreenChanged();

    if (m_vt) {
        vterm_screen_reset(m_vts, 1);
    }

    initPty();
    update();
}

void Terminal::sendResize(int cols, int rows) {
    if (cols < 40) cols = 40;
    if (rows < 15) rows = 15;
    if (cols > 1000) cols = 1000;
    if (rows > 1000) rows = 1000;

    if (cols == m_cols && rows == m_rows) {
        return;
    }

    m_cols = cols;
    m_rows = rows;

    if (m_vt) {
        vterm_set_size(m_vt, rows, cols);
        vterm_screen_flush_damage(m_vts);
    }

    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ioctl(m_ptmxFd, TIOCSWINSZ, &ws);

    update();
}

void Terminal::setTheme(const QString &bgHex, const QString &fgHex, bool isDark) {
    if (isDark) {
        m_colorMap[0] = bgHex;
        m_colorMap[1] = "#ed1515"; // Breeze Red
        m_colorMap[2] = "#11d116"; // Breeze Green
        m_colorMap[3] = "#f67400"; // Breeze Yellow/Orange
        m_colorMap[4] = "#1d99f3"; // Breeze Blue
        m_colorMap[5] = "#9b59b6"; // Breeze Magenta
        m_colorMap[6] = "#1abc9c"; // Breeze Cyan
        m_colorMap[7] = fgHex;     // Breeze Foreground (White)
        m_colorMap[8] = "#7f8c8d"; // Breeze Bright Black
        m_colorMap[9] = "#c0392b"; // Breeze Bright Red
        m_colorMap[10] = "#1cdc9a"; // Breeze Bright Green
        m_colorMap[11] = "#fdbc4b"; // Breeze Bright Yellow
        m_colorMap[12] = "#3daee9"; // Breeze Bright Blue
        m_colorMap[13] = "#8e44ad"; // Breeze Bright Magenta
        m_colorMap[14] = "#16a085"; // Breeze Bright Cyan
        m_colorMap[15] = fgHex;     // Breeze Bright White
    } else {
        m_colorMap[0] = bgHex;
        m_colorMap[1] = "#c0392b"; // Breeze Light Red
        m_colorMap[2] = "#27ae60"; // Breeze Light Green
        m_colorMap[3] = "#f67400"; // Breeze Light Yellow/Orange
        m_colorMap[4] = "#2980b9"; // Breeze Light Blue
        m_colorMap[5] = "#8e44ad"; // Breeze Light Magenta
        m_colorMap[6] = "#16a085"; // Breeze Light Cyan
        m_colorMap[7] = fgHex;     // Breeze Light Foreground
        m_colorMap[8] = "#7f8c8d"; // Breeze Light Bright Black
        m_colorMap[9] = "#ed1515"; // Breeze Light Bright Red
        m_colorMap[10] = "#11d116"; // Breeze Light Bright Green
        m_colorMap[11] = "#fdbc4b"; // Breeze Light Bright Yellow
        m_colorMap[12] = "#3daee9"; // Breeze Light Bright Blue
        m_colorMap[13] = "#9b59b6"; // Breeze Light Bright Magenta
        m_colorMap[14] = "#1abc9c"; // Breeze Light Bright Cyan
        m_colorMap[15] = fgHex;     // Breeze Light Bright White
    }
    
    // Set default colors in vterm state if initialized
    if (m_vt) {
        VTermColor default_fg, default_bg;
        // Default to indexed 7 (fg) and 0 (bg)
        vterm_color_indexed(&default_fg, 7);
        vterm_color_indexed(&default_bg, 0);
        vterm_state_set_default_colors(vterm_obtain_state(m_vt), &default_fg, &default_bg);
    }
    update();
}

void Terminal::scrollLines(int lines) {
    if (m_useAltScreen) return;
    m_scrollOffset += lines;
    if (m_scrollOffset < 0) {
        m_scrollOffset = 0;
    }
    int maxScroll = static_cast<int>(m_history.size());
    if (m_scrollOffset > maxScroll) {
        m_scrollOffset = maxScroll;
    }
    update();
}

QString Terminal::getColorHex(const VTermColor &color) {
    if (VTERM_COLOR_IS_INDEXED(&color)) {
        int idx = color.indexed.idx;
        if (idx >= 0 && idx < 16) {
            return m_colorMap[idx];
        }
    }
    if (VTERM_COLOR_IS_RGB(&color)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02x%02x%02x", color.rgb.red, color.rgb.green, color.rgb.blue);
        return QString(buf);
    }
    return QString();
}

void Terminal::paint(QPainter *painter) {
    if (!m_vts) return;

    QFont font(m_fontName, m_fontSize);
    painter->setFont(font);

    QFontMetricsF fm(font);
    double charWidth = fm.horizontalAdvance("X");
    double charHeight = fm.height();
    if (charWidth <= 0 || charHeight <= 0) return;

    double ascent = fm.ascent();

    QColor themeBg(m_colorMap[0]);
    painter->fillRect(boundingRect(), themeBg);

    int historySize = m_useAltScreen ? 0 : static_cast<int>(m_history.size());
    int firstVisibleVirtualLine = historySize - m_scrollOffset;

    for (int y = 0; y < m_rows; ++y) {
        int virtualY = firstVisibleVirtualLine + y;
        bool isHistorical = (!m_useAltScreen && virtualY < historySize);

        for (int x = 0; x < m_cols; ++x) {
            VTermScreenCell cell = {};
            bool gotCell = false;

            if (isHistorical) {
                if (virtualY >= 0 && virtualY < historySize) {
                    const auto &histLine = m_history[virtualY];
                    if (x < static_cast<int>(histLine.size())) {
                        cell = histLine[x];
                        gotCell = true;
                    }
                }
            } else {
                VTermPos pos = { virtualY - historySize, x };
                if (vterm_screen_get_cell(m_vts, pos, &cell)) {
                    gotCell = true;
                }
            }

            if (!gotCell) continue;

            VTermColor fgColor = cell.fg;
            VTermColor bgColor = cell.bg;

            QColor bgVal = themeBg;
            if (VTERM_COLOR_IS_DEFAULT_BG(&bgColor)) {
                bgVal = themeBg;
            } else {
                QString customHex = getColorHex(bgColor);
                if (!customHex.isEmpty()) {
                    bgVal = QColor(customHex);
                } else {
                    vterm_screen_convert_color_to_rgb(m_vts, &bgColor);
                    bgVal = QColor(getColorHex(bgColor));
                }
            }

            QColor fgVal = QColor(m_colorMap[7]);
            if (VTERM_COLOR_IS_DEFAULT_FG(&fgColor)) {
                fgVal = QColor(m_colorMap[7]);
            } else {
                QString customHex = getColorHex(fgColor);
                if (!customHex.isEmpty()) {
                    fgVal = QColor(customHex);
                } else {
                    vterm_screen_convert_color_to_rgb(m_vts, &fgColor);
                    fgVal = QColor(getColorHex(fgColor));
                }
            }

            if (cell.attrs.reverse) {
                std::swap(fgVal, bgVal);
            }

            QRectF cellRect(x * charWidth, y * charHeight, charWidth, charHeight);

            if (bgVal != themeBg) {
                painter->fillRect(cellRect, bgVal);
            }

            uint32_t cp = cell.chars[0];
            if (cp != 0 && cp != ' ') {
                painter->setPen(fgVal);

                QFont cellFont = font;
                cellFont.setBold(cell.attrs.bold);
                cellFont.setUnderline(cell.attrs.underline != VTERM_UNDERLINE_OFF);
                cellFont.setItalic(cell.attrs.italic);
                painter->setFont(cellFont);

                int len = 0;
                while (len < VTERM_MAX_CHARS_PER_CELL && cell.chars[len] != 0) {
                    len++;
                }

                QString charStr = QString::fromUcs4(reinterpret_cast<const char32_t*>(cell.chars), len);
                QPointF textPos(cellRect.left(), cellRect.top() + ascent);
                painter->drawText(textPos, charStr);
            }

            if (isCellSelected(x, virtualY)) {
                QColor selectionColor(61, 174, 233, 100); // Breeze blue with opacity
                painter->fillRect(cellRect, selectionColor);
            }
        }
    }

    if (m_isConnected && m_cursorVisible) {
        int visualCursorY = m_cy + (m_useAltScreen ? 0 : m_scrollOffset);
        if (visualCursorY >= 0 && visualCursorY < m_rows) {
            QRectF cursorRect(m_cx * charWidth, visualCursorY * charHeight, charWidth, charHeight);
            if (m_terminalFocused) {
                VTermPos pos = { m_cy, m_cx };
                VTermScreenCell cell = {};
                bool hasChar = false;
                if (vterm_screen_get_cell(m_vts, pos, &cell)) {
                    if (cell.chars[0] != 0 && cell.chars[0] != ' ') {
                        hasChar = true;
                    }
                }

                if (hasChar) {
                    QColor cursorColor("#5e6366");
                    painter->fillRect(cursorRect, cursorColor);

                    painter->setPen(QColor("#ffffff"));
                    QFont cellFont = font;
                    cellFont.setBold(cell.attrs.bold);
                    cellFont.setUnderline(cell.attrs.underline != VTERM_UNDERLINE_OFF);
                    cellFont.setItalic(cell.attrs.italic);
                    painter->setFont(cellFont);

                    int len = 0;
                    while (len < VTERM_MAX_CHARS_PER_CELL && cell.chars[len] != 0) {
                        len++;
                    }
                    QString charStr = QString::fromUcs4(reinterpret_cast<const char32_t*>(cell.chars), len);
                    QPointF textPos(cursorRect.left(), cursorRect.top() + ascent);
                    painter->drawText(textPos, charStr);
                } else {
                    QColor cursorColor("#ffffff");
                    painter->fillRect(cursorRect, cursorColor);
                }
            } else {
                QPen outlinePen(QColor(m_colorMap[7]), 1);
                painter->setPen(outlinePen);
                painter->drawRect(cursorRect.adjusted(0.5, 0.5, -0.5, -0.5));
            }
        }
    }
}

void Terminal::sendMouseEvent(int type, int button, int x, int y, bool isPressed) {
    if (!m_vts || (!m_mouseTrackingClick && !m_mouseTrackingDrag)) {
        return;
    }

    QFont font(m_fontName, m_fontSize);
    QFontMetricsF fm(font);
    double charWidth = fm.horizontalAdvance("X");
    double charHeight = fm.height();
    if (charWidth <= 0 || charHeight <= 0) return;

    int col = static_cast<int>(x / charWidth);
    int row = static_cast<int>(y / charHeight);

    if (col < 0) col = 0;
    if (col >= m_cols) col = m_cols - 1;
    if (row < 0) row = 0;
    if (row >= m_rows) row = m_rows - 1;

    VTermModifier mod = VTERM_MOD_NONE;

    if (type == 2) {
        // Mouse Drag
        vterm_mouse_move(m_vt, row, col, mod);
    } else {
        // Mouse Click: button is 0=Left, 1=Middle, 2=Right
        int btn = button + 1;
        vterm_mouse_button(m_vt, btn, isPressed, mod);
    }
}

void Terminal::startSelection(double x, double y) {
    if (m_clickTimer.isValid() && m_clickTimer.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
        m_clickCount++;
    } else {
        m_clickCount = 1;
    }
    m_clickTimer.start();

    qDebug() << "startSelection called: x =" << x << "y =" << y << "clickCount =" << m_clickCount;

    if (m_clickCount == 2) {
        selectWord(x, y);
        return;
    } else if (m_clickCount >= 3) {
        selectLine(x, y);
        return;
    }

    clearSelection();
    SelectionPoint pt = cellAt(x, y);
    pt.col = std::min(pt.col, getLineLength(pt.row));
    m_selectionStart = pt;
    m_selectionEnd = m_selectionStart;
    m_isSelecting = true;
    update();
}

void Terminal::updateSelection(double x, double y) {
    if (!m_isSelecting) return;
    SelectionPoint pt = cellAt(x, y);
    pt.col = std::min(pt.col, getLineLength(pt.row));
    if (pt.row != m_selectionEnd.row || pt.col != m_selectionEnd.col) {
        m_selectionEnd = pt;
        m_hasSelection = true;
        update();
    }
}

void Terminal::endSelection() {
    m_isSelecting = false;
    if (m_hasSelection) {
        QString text = getSelectedText();
        if (!text.isEmpty()) {
            QClipboard *clipboard = QGuiApplication::clipboard();
            if (clipboard) {
                clipboard->setText(text, QClipboard::Selection);
                clipboard->setText(text, QClipboard::Clipboard);
            }
        }
    }
}

void Terminal::clearSelection() {
    if (m_hasSelection) {
        m_hasSelection = false;
        update();
    }
}

QString Terminal::selectedText() const {
    return getSelectedText();
}

void Terminal::pasteFromSelection() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        QString text = clipboard->text(QClipboard::Selection);
        if (!text.isEmpty()) {
            sendInput(text);
        }
    }
}

void Terminal::pasteFromClipboard() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        QString text = clipboard->text(QClipboard::Clipboard);
        if (!text.isEmpty()) {
            sendInput(text);
        }
    }
}

Terminal::SelectionPoint Terminal::cellAt(double x, double y) const {
    QFont font(m_fontName, m_fontSize);
    QFontMetricsF fm(font);
    double charWidth = fm.horizontalAdvance("X");
    double charHeight = fm.height();
    if (charWidth <= 0 || charHeight <= 0) return {0, 0};

    int col = static_cast<int>(x / charWidth);
    int row = static_cast<int>(y / charHeight);

    if (col < 0) col = 0;
    if (col >= m_cols) col = m_cols - 1;
    if (row < 0) row = 0;
    if (row >= m_rows) row = m_rows - 1;

    int historySize = m_useAltScreen ? 0 : static_cast<int>(m_history.size());
    int virtualRow = (historySize - m_scrollOffset) + row;
    if (virtualRow < 0) virtualRow = 0;
    
    return { col, virtualRow };
}

int Terminal::getLineLength(int r) const {
    int historySize = m_useAltScreen ? 0 : static_cast<int>(m_history.size());
    int cols = m_cols;
    
    for (int c = cols - 1; c >= 0; --c) {
        VTermScreenCell cell = {};
        bool gotCell = false;

        if (r < historySize) {
            if (r >= 0 && r < static_cast<int>(m_history.size())) {
                const auto &histLine = m_history[r];
                if (c >= 0 && c < static_cast<int>(histLine.size())) {
                    cell = histLine[c];
                    gotCell = true;
                }
            }
        } else {
            VTermPos pos = { r - historySize, c };
            if (vterm_screen_get_cell(m_vts, pos, &cell)) {
                gotCell = true;
            }
        }

        if (gotCell) {
            uint32_t cp = cell.chars[0];
            if (cp != 0 && !QChar(cp).isSpace()) {
                return c + 1;
            }
        }
    }
    return 0;
}

bool Terminal::isCellSelected(int col, int row) const {
    if (!m_hasSelection) return false;

    SelectionPoint selStart = m_selectionStart;
    SelectionPoint selEnd = m_selectionEnd;
    if (selEnd.row < selStart.row || (selEnd.row == selStart.row && selEnd.col < selStart.col)) {
        std::swap(selStart, selEnd);
    }

    if (row < selStart.row || row > selEnd.row) return false;

    int lineLen = getLineLength(row);

    if (selStart.row == selEnd.row) {
        return col >= selStart.col && col < selEnd.col;
    }

    if (row == selStart.row) {
        return col >= selStart.col && col <= lineLen;
    }

    if (row == selEnd.row) {
        return col < selEnd.col;
    }

    return col <= lineLen;
}

QString Terminal::getSelectedText() const {
    if (!m_hasSelection) return QString();

    SelectionPoint selStart = m_selectionStart;
    SelectionPoint selEnd = m_selectionEnd;
    if (selEnd.row < selStart.row || (selEnd.row == selStart.row && selEnd.col < selStart.col)) {
        std::swap(const_cast<SelectionPoint&>(selStart), const_cast<SelectionPoint&>(selEnd));
    }

    QString text;
    int historySize = m_useAltScreen ? 0 : static_cast<int>(m_history.size());

    for (int r = selStart.row; r <= selEnd.row; ++r) {
        int startC = (r == selStart.row) ? selStart.col : 0;
        int endC = (r == selEnd.row) ? selEnd.col : m_cols;

        QString lineText;
        for (int c = startC; c < endC; ++c) {
            VTermScreenCell cell = {};
            bool gotCell = false;

            if (r < historySize) {
                if (r >= 0 && r < static_cast<int>(m_history.size())) {
                    const auto &histLine = m_history[r];
                    if (c >= 0 && c < static_cast<int>(histLine.size())) {
                        cell = histLine[c];
                        gotCell = true;
                    }
                }
            } else {
                VTermPos pos = { r - historySize, c };
                if (vterm_screen_get_cell(m_vts, pos, &cell)) {
                    gotCell = true;
                }
            }

            if (gotCell) {
                int len = 0;
                while (len < VTERM_MAX_CHARS_PER_CELL && cell.chars[len] != 0) {
                    len++;
                }
                if (len > 0) {
                    lineText += QString::fromUcs4(reinterpret_cast<const char32_t*>(cell.chars), len);
                } else {
                    lineText += " ";
                }
            } else {
                lineText += " ";
            }
        }

        while (!lineText.isEmpty() && lineText.at(lineText.length() - 1).isSpace()) {
            lineText.chop(1);
        }

        if (r < selEnd.row) {
            text += lineText + "\n";
        } else {
            text += lineText;
        }
    }

    return text;
}

bool Terminal::isPositionHoverable(double x, double y) const {
    QFont font(m_fontName, m_fontSize);
    QFontMetricsF fm(font);
    double charWidth = fm.horizontalAdvance("X");
    double charHeight = fm.height();
    if (charWidth <= 0 || charHeight <= 0) return false;

    int col = static_cast<int>(x / charWidth);
    int row = static_cast<int>(y / charHeight);

    if (col < 0 || col >= m_cols || row < 0 || row >= m_rows) return false;

    int historySize = m_useAltScreen ? 0 : static_cast<int>(m_history.size());
    int virtualRow = (historySize - m_scrollOffset) + row;
    if (virtualRow < 0) return false;

    int lineLen = 0;
    int r = virtualRow;
    
    for (int c = m_cols - 1; c >= 0; --c) {
        VTermScreenCell cell = {};
        bool gotCell = false;

        if (r < historySize) {
            if (r >= 0 && r < static_cast<int>(m_history.size())) {
                const auto &histLine = m_history[r];
                if (c >= 0 && c < static_cast<int>(histLine.size())) {
                    cell = histLine[c];
                    gotCell = true;
                }
            }
        } else {
            VTermPos pos = { r - historySize, c };
            if (vterm_screen_get_cell(m_vts, pos, &cell)) {
                gotCell = true;
            }
        }

        if (gotCell) {
            uint32_t cp = cell.chars[0];
            if (cp != 0 && !QChar(cp).isSpace()) {
                lineLen = c + 1;
                break;
            }
        }
    }

    return col < lineLen;
}

bool Terminal::getCellAt(int r, int c, VTermScreenCell *cell) const {
    int historySize = m_useAltScreen ? 0 : static_cast<int>(m_history.size());
    if (r < historySize) {
        if (r >= 0 && r < static_cast<int>(m_history.size())) {
            const auto &histLine = m_history[r];
            if (c >= 0 && c < static_cast<int>(histLine.size())) {
                *cell = histLine[c];
                return true;
            }
        }
    } else {
        VTermPos pos = { r - historySize, c };
        if (vterm_screen_get_cell(m_vts, pos, cell)) {
            return true;
        }
    }
    return false;
}

void Terminal::selectWord(double x, double y) {
    clearSelection();
    SelectionPoint pt = cellAt(x, y);
    int lineLen = getLineLength(pt.row);
    qDebug() << "selectWord: pt.col =" << pt.col << "pt.row =" << pt.row << "lineLen =" << lineLen;
    if (pt.col >= lineLen) {
        qDebug() << "selectWord: clicked column is beyond line length, returning";
        return;
    }

    int startCol = pt.col;
    int endCol = pt.col;

    auto isWordChar = [](uint32_t cp) {
        if (cp == 0 || QChar(cp).isSpace()) return false;
        QString wordSplitters = " \t\n`~!@#$%^&*()-_=+[{]}\\|;:'\",.<>/?";
        return !wordSplitters.contains(QChar(cp));
    };

    while (startCol > 0) {
        VTermScreenCell cell = {};
        if (getCellAt(pt.row, startCol - 1, &cell)) {
            bool isWord = isWordChar(cell.chars[0]);
            qDebug() << "Left check col:" << (startCol - 1) << "char:" << (char)cell.chars[0] << "code:" << cell.chars[0] << "isWord:" << isWord;
            if (isWord) {
                startCol--;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    while (endCol < lineLen - 1) {
        VTermScreenCell cell = {};
        if (getCellAt(pt.row, endCol + 1, &cell)) {
            bool isWord = isWordChar(cell.chars[0]);
            qDebug() << "Right check col:" << (endCol + 1) << "char:" << (char)cell.chars[0] << "code:" << cell.chars[0] << "isWord:" << isWord;
            if (isWord) {
                endCol++;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    m_selectionStart = { startCol, pt.row };
    m_selectionEnd = { endCol + 1, pt.row };
    m_hasSelection = true;
    m_isSelecting = false;
    qDebug() << "selectWord: selection set from" << startCol << "to" << (endCol + 1);
    update();
    endSelection();
}

void Terminal::selectLine(double x, double y) {
    clearSelection();
    SelectionPoint pt = cellAt(x, y);
    int lineLen = getLineLength(pt.row);
    
    m_selectionStart = { 0, pt.row };
    m_selectionEnd = { lineLen, pt.row };
    m_hasSelection = true;
    m_isSelecting = false;
    update();
    endSelection();
}


