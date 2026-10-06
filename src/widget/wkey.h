#pragma once

#include "control/controlproxy.h"
#include "preferences/colorpalettesettings.h"
#include "proto/keys.pb.h"
#include "widget/wlabel.h"

class WKey : public WLabel  {
    Q_OBJECT
  public:
    explicit WKey(const QString& group, UserSettingsPointer pConfig, QWidget* pParent = nullptr);

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
    void paintEvent(QPaintEvent* event) override;
};
