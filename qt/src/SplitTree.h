// A tab's split layout: a binary tree whose leaves are pane ids. Each
// split divides its rectangle between two children along one axis; the
// ratio is the first child's share. All geometry is in logical pixels.
#pragma once

#include <QHash>
#include <QList>
#include <QRectF>

#include <memory>

enum class SplitDirection { Right, Down, Left, Up };

class SplitTree
{
public:
    struct Divider {
        // Stable while the split exists; changes when the tree is rebuilt.
        int id;
        QRectF rect;
        // True for a vertical line between left and right children.
        bool vertical;
        // The rectangle the split divides, for converting drags to ratios.
        QRectF area;
    };

    explicit SplitTree(int pane);
    ~SplitTree();
    SplitTree(const SplitTree &) = delete;
    SplitTree &operator=(const SplitTree &) = delete;

    // Split `pane`, placing `newPane` on the given side. Returns false when
    // `pane` is not in the tree.
    bool split(int pane, SplitDirection direction, int newPane);
    // Remove `pane`; its sibling takes the parent's place. Returns false
    // when `pane` is not in the tree. The last pane cannot be removed.
    bool remove(int pane);
    bool contains(int pane) const;
    bool isEmpty() const;
    // Panes in reading order: left before right, top before bottom.
    QList<int> panes() const;

    // Each pane's rectangle within `area`, separated by `gap` pixels.
    // While a pane is zoomed it alone fills the area.
    QHash<int, QRectF> layout(const QRectF &area, qreal gap) const;
    QList<Divider> dividers(const QRectF &area, qreal gap) const;

    // The pane after or before `pane` in reading order, wrapping around.
    int next(int pane) const;
    int previous(int pane) const;
    // The nearest pane beside `pane` in `direction`, or -1.
    int neighbor(int pane, SplitDirection direction, const QRectF &area, qreal gap) const;

    // Move the divider of the nearest split along the direction's axis
    // that contains `pane` by `pixels`.
    bool resize(int pane, SplitDirection direction, qreal pixels, const QRectF &area, qreal gap);
    // Place a divider so its first child spans `ratio` of the split.
    bool setDividerRatio(int divider, qreal ratio);
    // Give every pane the same share along each axis.
    void equalize();

    int zoomed() const { return m_zoomed; }
    // Zoom `pane`, or clear the zoom with -1.
    void setZoomed(int pane);

private:
    struct Node;
    Node *find(int pane) const;
    QHash<int, QRectF> unzoomedLayout(const QRectF &area, qreal gap) const;
    std::unique_ptr<Node> m_root;
    int m_zoomed = -1;
    int m_nextDividerId = 1;
};
