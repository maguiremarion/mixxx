#pragma once


#include "wnumber.h"
#include "preferences/dialog/dlgprefdeck.h"

class ControlProxy;

class WNumberPos : public WNumber {
    Q_OBJECT

  public:
    explicit WNumberPos(const QString& group, QWidget* parent = nullptr);

  protected:
    void mousePressEvent(QMouseEvent* pEvent) override;
    void showEvent(QShowEvent* pEvent) override;

  private slots:
    void setValue(double dValue) override;
    void slotSetTimeElapsed(double);
    void slotTimeRemainingUpdated(double);
    void slotSetDisplayMode(double);
    void slotSetTimeFormat(double);
    void slotTrackLoadedChanged(double);

  private:

    TrackTime::DisplayMode m_displayMode;
    TrackTime::DisplayFormat m_displayFormat;

    double m_dOldTimeElapsed;
    ControlProxy* m_pTimeElapsed;
    ControlProxy* m_pTimeRemaining;
    ControlProxy* m_pShowTrackTimeRemaining;
    ControlProxy* m_pTimeFormat;
    ControlProxy* m_pTrackLoaded = nullptr; // created last: the constructor already calls slotSetTimeElapsed()
};
