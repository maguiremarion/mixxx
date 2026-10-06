#pragma once

#include <QBasicTimer>
#include <QModelIndex>
#include <QTreeView>

#include "library/library_decl.h"
#include "widget/wbasewidget.h"

class LibraryFeature;
class QPoint;

class WLibrarySidebar : public QTreeView, public WBaseWidget {
    Q_OBJECT
  public:
    explicit WLibrarySidebar(QWidget* parent = nullptr);

    /// Size in pixels of the feature icons, settable from the skin (qproperty-featureIconSize in
    /// the QSS). 0 = scale with the library font like stock Mixxx.
    Q_PROPERTY(int featureIconSize READ featureIconSize WRITE setFeatureIconSize)
    int featureIconSize() const {
        return m_featureIconSize;
    }
    void setFeatureIconSize(int size);

    void contextMenuEvent(QContextMenuEvent* pEvent) override;
    void dragMoveEvent(QDragMoveEvent* pEvent) override;
    void dragEnterEvent(QDragEnterEvent* pEvent) override;
    void dragLeaveEvent(QDragLeaveEvent* pEvent) override;
    void dropEvent(QDropEvent* pEvent) override;
    void keyPressEvent(QKeyEvent* pEvent) override;
    void mousePressEvent(QMouseEvent* pEvent) override;
    void focusInEvent(QFocusEvent* pEvent) override;
    void timerEvent(QTimerEvent* pEvent) override;
    void toggleSelectedItem();
    void renameSelectedItem();
    bool isLeafNodeSelected();
    bool isChildIndexSelected(const QModelIndex& index);
    bool isFeatureRootIndexSelected(LibraryFeature* pFeature);

  public slots:
    void selectIndex(const QModelIndex& index, bool scrollToIndex = true);
    void selectChildIndex(const QModelIndex&, bool selectItem = true);
    void slotSetFont(const QFont& font);
    void slotSetExpandOnHoverDelay(int delay);

  signals:
    void rightClicked(const QPoint&, const QModelIndex&);
    void renameItem(const QModelIndex&);
    void deleteItem(const QModelIndex&);
    FocusWidget setLibraryFocus(FocusWidget newFocus,
            Qt::FocusReason focusReason = Qt::OtherFocusReason);

  protected:
    bool event(QEvent* pEvent) override;
    void rowsInserted(const QModelIndex& parent, int start, int end) override;

  private:
    void focusSelectedIndex();
    QModelIndex selectedIndex();

    void toggleDragHoverPropertyAndUpdateStyle(bool enabled);
    void resetHoverIndexAndDragMoveResult();

    QBasicTimer m_expandTimer;
    int m_hoverExpandDelay;
    QModelIndex m_hoverIndex;
    bool m_lastDragMoveAccepted;
    int m_featureIconSize = 0;
    // A feature whose entry was tapped before its children existed (Rekordbox finds its drives
    // after the tap): expand it as soon as they appear.
    QPersistentModelIndex m_expandWhenChildrenAppear;
};
