#include "library/tabledelegates/keydelegate.h"

#include <QPainter>
#include <algorithm>

#include <QPainter>
#include <QStyle>
#include <QTableView>

#include "library/trackmodel.h"
#include "moc_keydelegate.cpp"

namespace {
// Unicode symbols for tuning indicators
const QString kTuningSymbol432Hz = QStringLiteral("\u2727"); // ✧ (sparkle) for 432Hz
const QString kTuningSymbolLow = QStringLiteral("\u2193");   // ↓ (arrow down) for <440Hz
const QString kTuningSymbolHigh = QStringLiteral("\u2191");  // ↑ (arrow up) for >440Hz
constexpr int kTuningSymbolWidth = 14;
constexpr double kStandardTuningHz = 440.0;
constexpr double k432Hz = 432.0;
constexpr double kTuningToleranceHz = 2.5; // 2.5 Hz equals roughly 10 cents for these frequencies
constexpr double kStandardTuningLowHz = kStandardTuningHz - kTuningToleranceHz;
constexpr double kStandardTuningHighHz = kStandardTuningHz + kTuningToleranceHz;
constexpr double k432LowHz = k432Hz - kTuningToleranceHz;
constexpr double k432HighHz = k432Hz + kTuningToleranceHz;
} // namespace

void KeyDelegate::paintItem(
        QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const {
    paintItemBackground(painter, option, index);

    const QString keyText = index.data().value<QString>();
    const QVariantMap colorRect = index.data(Qt::DecorationRole).value<QVariantMap>();
    const double tuningFrequencyHz = index.data(TrackModel::kTuningFrequencyRole).toDouble();
    const QColor colorTop = colorRect["top"].value<QColor>();

    // Determine which tuning symbol to show (if any)
    QString tuningSymbol;
    QColor symbolColor;
    if (tuningFrequencyHz >= k432LowHz && tuningFrequencyHz <= k432HighHz) {
        // 432Hz (with tolerance) gets the sparkle symbol
        tuningSymbol = kTuningSymbol432Hz;
        symbolColor = QColor(218, 165, 32); // Golden color
    } else if (tuningFrequencyHz > 0.0 && tuningFrequencyHz < kStandardTuningLowHz) {
        // Lower than 440Hz gets arrow down
        tuningSymbol = kTuningSymbolLow;
        symbolColor = QColor(100, 149, 237); // Cornflower blue
    } else if (tuningFrequencyHz > kStandardTuningHighHz) {
        // Higher than 440Hz gets arrow up
        tuningSymbol = kTuningSymbolHigh;
        symbolColor = QColor(255, 99, 71); // Tomato red
    }

    // Laid out from the right edge, like the key label in the deck sidebar: [text][colour square]
    // with an optional tuning symbol at the very end. (It used to be a colour bar against the
    // left edge and left-aligned text, which crowded the neighbouring column.)
    constexpr int kPad = 8;
    int right = option.rect.right() + 1 - kPad;

    if (!tuningSymbol.isEmpty()) {
        painter->save();
        if (option.state & QStyle::State_Selected) {
            // Use a brighter color when selected
            symbolColor = symbolColor.lighter(130);
        }
        painter->setPen(symbolColor);
        QFont symbolFont = option.font;
        symbolFont.setBold(true);
        painter->setFont(symbolFont);
        painter->drawText(
                right - kTuningSymbolWidth,
                option.rect.y(),
                kTuningSymbolWidth,
                option.rect.height(),
                Qt::AlignVCenter | Qt::AlignRight,
                tuningSymbol);
        painter->restore();
        right -= kTuningSymbolWidth;
    }

    if (colorTop.isValid()) {
        // Pale rounded square in the key's colour (45 % of the way to white)
        const QColor pale = QColor::fromRgbF(colorTop.redF() * 0.55 + 0.45,
                colorTop.greenF() * 0.55 + 0.45,
                colorTop.blueF() * 0.55 + 0.45);
        const int squareSize = std::min(12, std::max(8, option.fontMetrics.height() / 2));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(pale);
        painter->drawRoundedRect(QRectF(right - squareSize,
                                         option.rect.y() + (option.rect.height() - squareSize) / 2.0,
                                         squareSize,
                                         squareSize),
                squareSize * 0.3,
                squareSize * 0.3);
        painter->restore();
        right -= squareSize + 6;
    }

    // Display the key text with the user-provided notation, right-aligned
    const int textLeft = option.rect.x() + kPad;
    const int textWidth = std::max(0, right - textLeft);
    QString elidedText = option.fontMetrics.elidedText(keyText, Qt::ElideRight, textWidth);

    // This is not picking up the 'missing' or 'played' text color via
    // ForegroundRole from BaseTrackTableModel::data().
    // Set the palette colors manually and select the appropriate one.
    QStyleOptionViewItem opt = option;
    setTextColor(opt, index);
    if (opt.state & QStyle::State_Selected) {
        painter->setPen(QPen(opt.palette.highlightedText().color()));
    } else {
        painter->setPen(QPen(opt.palette.text().color()));
    }

    painter->drawText(textLeft,
            option.rect.y(),
            textWidth,
            option.rect.height(),
            Qt::AlignVCenter | Qt::AlignRight,
            elidedText);

    // Draw a border if the key cell has focus
    if (option.state & QStyle::State_HasFocus) {
        drawBorder(painter, m_focusBorderColor, option.rect);
    }
}
