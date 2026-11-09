#pragma once

#include <QWidget>

#include "library/libraryview.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

// Browse > Bluetooth: pair a phone (the Pi is invisible to phones until PAIR A PHONE is tapped, for 3 minutes),
// who is connected, the phones that were paired and FORGET. It looks like the Auto DJ page (same style names).
class DlgBluetooth : public QWidget, public LibraryView {
    Q_OBJECT
  public:
    explicit DlgBluetooth(QWidget* pParent);

    // LibraryView: the library widget only shows a page that is one
    void onShow() override;
    bool hasFocus() const override {
        return QWidget::hasFocus();
    }

  private slots:
    void refresh();

  private:
    QLabel* m_pPairText;
    QPushButton* m_pPairButton;
    QPushButton* m_pCancelButton;
    QLabel* m_pPhonesCaption;
    QVBoxLayout* m_pDeviceRows;
};
