#pragma once

#include <QColor>

#include "control/controlproxy.h"
#include "preferences/colorpalettesettings.h"
#include "proto/keys.pb.h"
#include "widget/wlabel.h"

class WKey : public WLabel  {
    Q_OBJECT
  public:
    explicit WKey(const QString& group, UserSettingsPointer pConfig, QWidget* pParent = nullptr);

    /// Colour of the key text, set by the skin (qproperty-keyTextColor in the QSS). The label draws
    /// its own text, and the colour a QSS `color:` rule leaves in the palette differs between
    /// styles: it came out black on the Pi (Fusion) while fine on macOS.
    Q_PROPERTY(QColor keyTextColor READ keyTextColor WRITE setKeyTextColor)
    QColor keyTextColor() const {
        return m_keyTextColor;
    }
    void setKeyTextColor(const QColor& color) {
        m_keyTextColor = color;
        update();
    }

    void onConnectedControlChanged(double dParameter, double dValue) override;
    void setup(const QDomNode& node, const SkinContext& context) override;

  protected:
    void mousePressEvent(QMouseEvent* pEvent) override;

  private slots:
    void setValue();
    void keyNotationChanged(double dValue);
    void setCents();

  private:
    double m_diff_cents;
    bool m_displayCents;
    bool m_displayKey;
    ControlProxy m_keyNotation;
    // [Skin],key_show_camelot: 1 = show keys as 1A / 12B, 0 = the usual notation. One control
    // shared by every key label (a tap on any of them flips it), saved between runs.
    ControlProxy m_showCamelot;
    ControlProxy m_engineKeyDistance;
    ControlProxy m_engineKey;
    ColorPaletteSettings m_colorPaletteSettings;
    mixxx::track::io::key::ChromaticKey m_key;
    QColor m_keyTextColor = QColor(0xe5, 0xe6, 0xea);
    void paintEvent(QPaintEvent* event) override;
};
