#include "widget/wlibrarysidebar.h"

#include <QHeaderView>
#include <QPen>
#include <QPainter>
#include <QScroller>
#include <QScrollerProperties>
#include <QUrl>
#include <QtDebug>

#include "library/library_prefs.h"
#include "library/sidebarmodel.h"
#include "moc_wlibrarysidebar.cpp"
#include "util/defs.h"
#include "util/dnd.h"

WLibrarySidebar::WLibrarySidebar(QWidget* parent)
        : QTreeView(parent),
          WBaseWidget(this),
          m_hoverExpandDelay(mixxx::library::prefs::kSidebarHoverExpandDelayDefault),
          m_lastDragMoveAccepted(false) {
    qRegisterMetaType<FocusWidget>("FocusWidget");
    //Set some properties
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    //Drag and drop setup
    setDragEnabled(false);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(true);
    setAcceptDrops(true);
    setAutoScroll(true);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    // One column exactly as wide as the sidebar: a name that is too long is cut off with "..."
    // instead of making the sidebar scroll sideways.
    header()->setStretchLastSection(true);
    header()->setSectionResizeMode(QHeaderView::Stretch);
    header()->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTextElideMode(Qt::ElideRight);

    // Finger scrolling, like the track table.
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    QScroller::grabGesture(viewport(), QScroller::LeftMouseButtonGesture);
    QScrollerProperties scrollerProps = QScroller::scroller(viewport())->scrollerProperties();
    scrollerProps.setScrollMetric(QScrollerProperties::MousePressEventDelay, 0.08);
    scrollerProps.setScrollMetric(QScrollerProperties::DragStartDistance, 0.004);
    scrollerProps.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
            QScrollerProperties::OvershootAlwaysOff);
    scrollerProps.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
            QScrollerProperties::OvershootAlwaysOff);
    QScroller::scroller(viewport())->setScrollerProperties(scrollerProps);

    // One tap on an entry toggles it open/shut: a double click is too hard on a touchscreen.
    // The default "double click toggles" would fight with this, so it is off. An entry whose
    // children don't exist yet (Rekordbox finds its drives after the tap) opens as soon as they
    // appear.
    setExpandsOnDoubleClick(false);
    connect(this, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        if (isExpanded(index)) {
            collapse(index);
        } else if (model()->hasChildren(index)) {
            expand(index);
        } else {
            m_expandWhenChildrenAppear = index;
        }
    });
}

void WLibrarySidebar::drawBranches(
        QPainter* pPainter, const QRect& rect, const QModelIndex& index) const {
    // The arrows (or nothing) first, as usual.
    QTreeView::drawBranches(pPainter, rect, index);

    // Connector lines for children only: a vertical line down from the parent's arrow, with a
    // short stub into each child, so it is obvious which entries belong to which. The top level
    // entries are not joined to each other.
    int depth = 0;
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) {
        ++depth;
    }
    if (depth == 0) {
        return;
    }

    const int indent = indentation();
    const int midY = rect.top() + rect.height() / 2;
    const bool hasChildren = model()->hasChildren(index);

    pPainter->save();
    pPainter->setRenderHint(QPainter::Antialiasing, false);
    pPainter->setPen(QPen(m_branchLineColor, 1));

    // Walk up through the item and its ancestors: column c (0 = leftmost) holds the line of the
    // children of the depth-c ancestor. It continues past this row when the entry on this
    // branch at depth c + 1 has a following sibling.
    QModelIndex branchEntry = index;
    for (int column = depth - 1; column >= 0; --column) {
        const int centerX = rect.left() + column * indent + indent / 2;
        const bool hasNextSibling = branchEntry.siblingAtRow(branchEntry.row() + 1).isValid();
        if (column == depth - 1) {
            // This entry's own connector: down from the top to its middle (and on to the bottom
            // if more siblings follow), then a stub into the entry.
            pPainter->drawLine(centerX, rect.top(), centerX, hasNextSibling ? rect.bottom() : midY);
            // The stub runs to the entry's text; if the entry has children of its own, its arrow
            // sits in the next column, so stop at that column instead.
            const int stubEnd = hasChildren ? rect.left() + depth * indent
                                            : rect.left() + (depth + 1) * indent - 2;
            pPainter->drawLine(centerX, midY, stubEnd, midY);
        } else if (hasNextSibling) {
            pPainter->drawLine(centerX, rect.top(), centerX, rect.bottom());
        }
        branchEntry = branchEntry.parent();
    }
    pPainter->restore();
}

void WLibrarySidebar::rowsInserted(const QModelIndex& parent, int start, int end) {
    QTreeView::rowsInserted(parent, start, end);
    if (parent.isValid() && m_expandWhenChildrenAppear.isValid() &&
            parent == QModelIndex(m_expandWhenChildrenAppear)) {
        m_expandWhenChildrenAppear = QModelIndex();
        expand(parent);
    }
}

void WLibrarySidebar::setFeatureIconSize(int size) {
    m_featureIconSize = size;
    if (size > 0) {
        setIconSize(QSize(size, size));
    }
}

void WLibrarySidebar::contextMenuEvent(QContextMenuEvent* pEvent) {
    // if (pEvent->state() & Qt::RightButton) { //Dis shiz don werk on windowze
    QModelIndex clickedIndex = indexAt(pEvent->pos());
    if (!clickedIndex.isValid()) {
        return;
    }
    // Use this instead of setCurrentIndex() to keep current selection
    selectionModel()->setCurrentIndex(clickedIndex, QItemSelectionModel::NoUpdate);
    pEvent->accept();
    emit rightClicked(pEvent->globalPos(), clickedIndex);
    //}
}

/// Drag enter event, happens when a dragged item enters the track sources view
void WLibrarySidebar::dragEnterEvent(QDragEnterEvent* pEvent) {
    qDebug() << "WLibrarySidebar::dragEnterEvent" << pEvent->mimeData()->formats();
    resetHoverIndexAndDragMoveResult();
    if (pEvent->mimeData()->hasUrls()) {
        // We don't have a way to ask the LibraryFeatures whether to accept a
        // drag so for now we accept all drags. Since almost every
        // LibraryFeature accepts all files in the drop and accepts playlist
        // drops we default to those flags to DragAndDropHelper.
        // FIXME Unless the cursor is steady after entering the sidebar (which
        // is veryhard to achieve for humans) QDragEnterEvent is followed by one
        // or more QDragMoveEvent, so don't check here at all and rely on dragMove?
        if (DragAndDropHelper::urlsContainSupportedTrackFiles(pEvent->mimeData()->urls(), true)) {
            pEvent->acceptProposedAction();
            return;
        }
    }
    pEvent->ignore();
    // QTreeView::dragEnterEvent(pEvent);
}

/// Drag leave event, happens when leaving and when the drag is aborted, eg. with Esc.
/// We override this only to reset the drag hover property.
void WLibrarySidebar::dragLeaveEvent(QDragLeaveEvent* pEvent) {
    // qDebug() << "WLibrarySidebar::dragLeaveEvent";
    toggleDragHoverPropertyAndUpdateStyle(false);

    QTreeView::dragLeaveEvent(pEvent);
}

/// Drag move event, happens when a dragged item hovers over the track sources view...
void WLibrarySidebar::dragMoveEvent(QDragMoveEvent* pEvent) {
    // qDebug() << "WLibrarySidebar::dragMoveEvent" << pEvent->mimeData()->formats();
    toggleDragHoverPropertyAndUpdateStyle(true);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPoint pos = pEvent->position().toPoint();
#else
    QPoint pos = pEvent->pos();
#endif
    const QModelIndex index = indexAt(pos);
    if (m_hoverIndex == index) {
        m_lastDragMoveAccepted ? pEvent->acceptProposedAction() : pEvent->ignore();
        return;
    }

    m_hoverIndex = index;

    if (m_hoverExpandDelay >= 0) {
        // Timeout of < 0 disables auto-expand
        m_expandTimer.stop();
        m_expandTimer.start(m_hoverExpandDelay, this);
    }

    // This has to be here instead of after, otherwise all drags will be
    // rejected -- rryan 3/2011
    QTreeView::dragMoveEvent(pEvent);
    if (!pEvent->mimeData()->hasUrls()) {
        pEvent->ignore();
        m_lastDragMoveAccepted = false;
        return;
    }

    const QList<QUrl> urls = pEvent->mimeData()->urls();
    // Drag and drop within this widget
    if ((pEvent->source() == this) && (pEvent->possibleActions() & Qt::MoveAction)) {
        // Do nothing.
        m_lastDragMoveAccepted = false;
        pEvent->ignore();
        return;
    }

    SidebarModel* pSidebarModel = qobject_cast<SidebarModel*>(model());
    VERIFY_OR_DEBUG_ASSERT(pSidebarModel) {
        m_lastDragMoveAccepted = false;
        pEvent->ignore();
        return;
    }
    if (pSidebarModel->dragMoveAccept(index, urls)) {
        m_lastDragMoveAccepted = true;
        pEvent->acceptProposedAction();
    } else {
        m_lastDragMoveAccepted = false;
        pEvent->ignore();
    }
}

void WLibrarySidebar::timerEvent(QTimerEvent* pEvent) {
    if (pEvent->timerId() == m_expandTimer.timerId()) {
        QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
        if (viewport()->rect().contains(pos)) {
            QModelIndex index = indexAt(pos);
            if (m_hoverIndex == index) {
                setExpanded(index, !isExpanded(index));
            }
        }
        m_expandTimer.stop();
        return;
    }
    QTreeView::timerEvent(pEvent);
}

// Drag-and-drop "drop" event. Occurs when something is dropped onto the track sources view
void WLibrarySidebar::dropEvent(QDropEvent* pEvent) {
    // qDebug() << "WLibrarySidebar::dropEvent";
    resetHoverIndexAndDragMoveResult();
    toggleDragHoverPropertyAndUpdateStyle(false);

    if (!pEvent->mimeData()->hasUrls()) {
        pEvent->ignore();
        return;
    }
    // Drag and drop within this widget
    if ((pEvent->source() == this) && (pEvent->possibleActions() & Qt::MoveAction)) {
        // Do nothing.
        pEvent->ignore();
        return;
    }
    // Drag-and-drop from an external application (eg. a file manager) or the
    // track table widget onto the sidebar.
    // Reset the selected items (if you had anything highlighted, it clears it)
    // this->selectionModel()->clear();
    SidebarModel* pSidebarModel = qobject_cast<SidebarModel*>(model());
    VERIFY_OR_DEBUG_ASSERT(pSidebarModel) {
        pEvent->ignore();
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPoint pos = pEvent->position().toPoint();
#else
    QPoint pos = pEvent->pos();
#endif

    const QModelIndex destIndex = indexAt(pos);
    // pEvent->source() will return NULL if something is dropped from
    // a different application
    const QList<QUrl> urls = pEvent->mimeData()->urls();
    if (pSidebarModel->dropAccept(destIndex, urls, pEvent->source())) {
        pEvent->acceptProposedAction();
    } else {
        pEvent->ignore();
    }
}

void WLibrarySidebar::toggleDragHoverPropertyAndUpdateStyle(bool enabled) {
    // Set a custom QWidget property that allows to style drag-hovered items.
    // WLibrarySidebar[dragHover="true"]::item:hover {
    //   border: 1px solid white;
    // }
    // Then force-refresh the style.
    setProperty("dragHover", enabled);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void WLibrarySidebar::resetHoverIndexAndDragMoveResult() {
    m_hoverIndex = QModelIndex();
    m_lastDragMoveAccepted = false;
}

void WLibrarySidebar::renameSelectedItem() {
    // Rename crate or playlist (internal, external, history)
    QModelIndex selIndex = selectedIndex();
    if (!selIndex.isValid()) {
        return;
    }
    emit renameItem(selIndex);
    return;
}

void WLibrarySidebar::toggleSelectedItem() {
    QModelIndex index = selectedIndex();
    if (index.isValid()) {
        // Activate the item so its content shows in the main library.
        emit clicked(index);
        // Expand or collapse the item as necessary.
        setExpanded(index, !isExpanded(index));
    }
}

bool WLibrarySidebar::isLeafNodeSelected() {
    QModelIndex index = selectedIndex();
    if (index.isValid()) {
        if(!index.model()->hasChildren(index)) {
            return true;
        }
        const SidebarModel* pSidebarModel = qobject_cast<const SidebarModel*>(index.model());
        if (pSidebarModel) {
            return pSidebarModel->hasTrackTable(index);
        }
    }
    return false;
}

bool WLibrarySidebar::isChildIndexSelected(const QModelIndex& index) {
    // qDebug() << "WLibrarySidebar::isChildIndexSelected" << index;
    QModelIndex selIndex = selectedIndex();
    if (!selIndex.isValid()) {
        return false;
    }
    SidebarModel* pSidebarModel = qobject_cast<SidebarModel*>(model());
    VERIFY_OR_DEBUG_ASSERT(pSidebarModel) {
        // qDebug() << " >> model() is not SidebarModel";
        return false;
    }
    QModelIndex translated = pSidebarModel->translateChildIndex(index);
    if (!translated.isValid()) {
        // qDebug() << " >> index can't be translated";
        return false;
    }
    return translated == selIndex;
}

bool WLibrarySidebar::isFeatureRootIndexSelected(LibraryFeature* pFeature) {
    // qDebug() << "WLibrarySidebar::isFeatureRootIndexSelected";
    QModelIndex selIndex = selectedIndex();
    if (!selIndex.isValid()) {
        return false;
    }
    SidebarModel* pSidebarModel = qobject_cast<SidebarModel*>(model());
    VERIFY_OR_DEBUG_ASSERT(pSidebarModel) {
        return false;
    }
    const QModelIndex rootIndex = pSidebarModel->getFeatureRootIndex(pFeature);
    return rootIndex == selIndex;
}

/// Invoked by actual keypresses (requires widget focus) and emulated keypresses
/// sent by LibraryControl
void WLibrarySidebar::keyPressEvent(QKeyEvent* pEvent) {
    // TODO(XXX) Should first keyEvent ensure previous item has focus? I.e. if the selected
    // item is not focused, require second press to perform the desired action.

    SidebarModel* pSidebarModel = qobject_cast<SidebarModel*>(model());
    QModelIndex selIndex = selectedIndex();
    if (pSidebarModel && selIndex.isValid() && pEvent->matches(QKeySequence::Paste)) {
        pSidebarModel->paste(selIndex);
        return;
    }

    focusSelectedIndex();

    switch (pEvent->key()) {
    case Qt::Key_Return:
        toggleSelectedItem();
        return;
    case Qt::Key_Down:
    case Qt::Key_Up:
    case Qt::Key_PageDown:
    case Qt::Key_PageUp:
    case Qt::Key_End:
    case Qt::Key_Home: {
        // Let the tree view move up and down for us.
        QTreeView::keyPressEvent(pEvent);
        // After the selection changed force-activate (click) the newly selected
        // item to save us from having to push "Enter".
        QModelIndex selIndex = selectedIndex();
        if (!selIndex.isValid()) {
            return;
        }
        // Ensure the new selection is visible even if it was already selected/
        // focused, like when the topmost item was selected but out of sight and
        // we pressed Up, Home or PageUp.
        scrollTo(selIndex);
        emit pressed(selIndex);
        return;
    }
    case Qt::Key_Right: {
        if (pEvent->modifiers() & Qt::ControlModifier) {
            emit setLibraryFocus(FocusWidget::TracksTable);
        } else {
            QTreeView::keyPressEvent(pEvent);
        }
        return;
    }
    case Qt::Key_Left: {
        // If an expanded item is selected let QTreeView collapse it
        QModelIndex selIndex = selectedIndex();
        if (!selIndex.isValid()) {
            return;
        }
        // collapse knot
        if (isExpanded(selIndex)) {
            QTreeView::keyPressEvent(pEvent);
            return;
        }
        // Else jump to its parent and activate it
        QModelIndex parentIndex = selIndex.parent();
        if (parentIndex.isValid()) {
            selectIndex(parentIndex);
            emit pressed(parentIndex);
        }
        return;
    }
    case Qt::Key_Escape:
        // Focus tracks table
        emit setLibraryFocus(FocusWidget::TracksTable);
        return;
    case kRenameSidebarItemShortcutKey: { // F2
        renameSelectedItem();
        return;
    }
    case kHideRemoveShortcutKey: { // Del (macOS: Cmd+Backspace)
        // Delete crate or playlist (internal, external, history)
        if (pEvent->modifiers() != kHideRemoveShortcutModifier) {
            return;
        }
        QModelIndex selIndex = selectedIndex();
        if (!selIndex.isValid()) {
            return;
        }
        emit deleteItem(selIndex);
        return;
    }
    default:
        QTreeView::keyPressEvent(pEvent);
    }
}

void WLibrarySidebar::mousePressEvent(QMouseEvent* pEvent) {
    // handle right click only in contextMenuEvent() to not select the clicked index
    if (pEvent->buttons().testFlag(Qt::RightButton)) {
        return;
    }
    QTreeView::mousePressEvent(pEvent);
}

void WLibrarySidebar::focusInEvent(QFocusEvent* pEvent) {
    // Clear the current index, i.e. remove the focus indicator
    selectionModel()->clearCurrentIndex();
    QTreeView::focusInEvent(pEvent);
}

void WLibrarySidebar::selectIndex(const QModelIndex& index, bool scrollToIndex) {
    // qDebug() << "WLibrarySidebar::selectIndex" << index << scrollToIndex;
    if (!index.isValid()) {
        return;
    }
    auto* pModel = new QItemSelectionModel(model());
    pModel->select(index, QItemSelectionModel::Select);
    if (selectionModel()) {
        selectionModel()->deleteLater();
    }
    if (index.parent().isValid()) {
        expand(index.parent());
    }
    setSelectionModel(pModel);
    if (!scrollToIndex) {
        // With auto-scroll enabled, setCurrentIndex() would scroll there.
        // Disable (and re-enable if we don't want to scroll, e.g. when selecting
        // AutoDJ from the menubar or during startup
        setAutoScroll(false);
    }
    setCurrentIndex(index);
    if (scrollToIndex) {
        scrollTo(index);
    } else {
        setAutoScroll(true);
    }
}

/// Selects a child index from a feature and ensures visibility
void WLibrarySidebar::selectChildIndex(const QModelIndex& index, bool selectItem) {
    SidebarModel* pSidebarModel = qobject_cast<SidebarModel*>(model());
    VERIFY_OR_DEBUG_ASSERT(pSidebarModel) {
        qDebug() << "model() is not SidebarModel";
        return;
    }
    QModelIndex translated = pSidebarModel->translateChildIndex(index);
    if (!translated.isValid()) {
        return;
    }

    if (selectItem) {
        auto* pModel = new QItemSelectionModel(pSidebarModel);
        pModel->select(translated, QItemSelectionModel::Select);
        if (selectionModel()) {
            selectionModel()->deleteLater();
        }
        setSelectionModel(pModel);
        setCurrentIndex(translated);
    }

    QModelIndex parentIndex = translated.parent();
    while (parentIndex.isValid()) {
        expand(parentIndex);
        parentIndex = parentIndex.parent();
    }
    scrollTo(translated, EnsureVisible);
}

QModelIndex WLibrarySidebar::selectedIndex() {
    QModelIndexList selectedIndices = selectionModel()->selectedRows();
    if (selectedIndices.isEmpty()) {
        return QModelIndex();
    }
    QModelIndex selIndex = selectedIndices.first();
    DEBUG_ASSERT(selIndex.isValid());
    return selIndex;
}

/// Refocus the selected item after right-click
void WLibrarySidebar::focusSelectedIndex() {
    // After the context menu was activated (and closed, with or without clicking
    // an action), the currentIndex is the right-clicked item.
    // If if the currentIndex is not selected, make the selection the currentIndex
    QModelIndex selIndex = selectedIndex();
    if (selIndex.isValid() && selIndex != selectionModel()->currentIndex()) {
        setCurrentIndex(selIndex);
    }
}

bool WLibrarySidebar::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    } else if (pEvent->type() == QEvent::LayoutRequest ||
            pEvent->type() == QEvent::Resize) {
        // Force-resize the header to expand the item's clickable area.
        //
        // Reason:
        // Currently, the sidebar header expands to the width of the widest item.
        // If the sidebar is wider than that, there's some space right next to
        // items that does not respond to clicks. This is somewhat frustration as
        // it is perceived inconsistent with the state when e.g. Playlist are
        // expanded and the entire 'Tracks' row responds to clicks.
        //
        // Desired appearance & behavior:
        // * full-width items (for click success)
        // * full item text (no elide)
        // * show horizontal scrollbars as needed
        //
        // Unfortunately, there's no combination of
        //   header()->setStretchLastSection(bool);
        //   header()->setSectionResizeMode(QHeaderView::ResizeMode);
        // to achieve that.
        //
        // Though we can listen to LayoutRequest and adjust the headers minimum
        // section size to viewport width (-1 for section separator?).
        // This event occurs after Show, Resize or model data change.
        header()->setMinimumSectionSize(viewport()->width() - 1);
    }
    return QTreeView::event(pEvent);
}

void WLibrarySidebar::slotSetFont(const QFont& font) {
    setFont(font);
    // Resize the feature icons to be a bit taller than the label's capital
    int iconSize = m_featureIconSize > 0
            ? m_featureIconSize
            : static_cast<int>(QFontMetrics(font).height() * 0.8);
    setIconSize(QSize(iconSize, iconSize));
}

void WLibrarySidebar::slotSetExpandOnHoverDelay(int delay) {
    m_hoverExpandDelay = delay;
}
