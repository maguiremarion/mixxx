#pragma once

#include <QPointer>
#include <QTextBrowser>

#include "library/library_decl.h"
#include "library/libraryview.h"

class QPushButton;

class WLibraryTextBrowser : public QTextBrowser, public LibraryView {
    Q_OBJECT
  public:
    explicit WLibraryTextBrowser(QWidget* parent = nullptr);
    void onShow() override {}
    bool hasFocus() const override;
    void setFocus() override;
    void keyPressEvent(QKeyEvent* event) override;

    /// The "Not seeing your drive?" section of the start pages (Rekordbox, Serato, Computer): a line
    /// of text and room for a real, finger-sized REFRESH DRIVES button, which this browser places
    /// over that room (an HTML page cannot hold a button).
    static QString refreshDrivesSectionHtml();

  protected:
    void resizeEvent(QResizeEvent* pEvent) override;
    void showEvent(QShowEvent* pEvent) override;

  private slots:
    void updateDriveButton();

  private:
    QPointer<QPushButton> m_pDriveButton;

  signals:
    FocusWidget setLibraryFocus(FocusWidget newFocus);
};
