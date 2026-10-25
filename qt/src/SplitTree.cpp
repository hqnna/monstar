#include "SplitTree.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {

constexpr qreal minRatio = 0.05;
constexpr qreal maxRatio = 0.95;

bool isHorizontal(SplitDirection direction)
{
    return direction == SplitDirection::Left || direction == SplitDirection::Right;
}

} // namespace

struct SplitTree::Node {
    // Leaf when `first` is null.
    int pane = -1;
    // A vertical divider: children are side by side.
    bool vertical = false;
    qreal ratio = 0.5;
    int dividerId = 0;
    std::unique_ptr<Node> first;
    std::unique_ptr<Node> second;
    Node *parent = nullptr;

    bool isLeaf() const { return !first; }

    // Children's rectangles within `area`, leaving `gap` between them.
    std::pair<QRectF, QRectF> childRects(const QRectF &area, qreal gap) const
    {
        if (vertical) {
            const qreal span = std::max<qreal>(0, area.width() - gap);
            const qreal a = std::round(span * ratio);
            return {QRectF(area.x(), area.y(), a, area.height()),
                    QRectF(area.x() + a + gap, area.y(), span - a, area.height())};
        }
        const qreal span = std::max<qreal>(0, area.height() - gap);
        const qreal a = std::round(span * ratio);
        return {QRectF(area.x(), area.y(), area.width(), a),
                QRectF(area.x(), area.y() + a + gap, area.width(), span - a)};
    }
};

SplitTree::SplitTree(int pane)
    : m_root(std::make_unique<Node>())
{
    m_root->pane = pane;
}

SplitTree::~SplitTree() = default;

SplitTree::Node *SplitTree::find(int pane) const
{
    std::function<Node *(Node *)> visit = [&](Node *node) -> Node * {
        if (!node)
            return nullptr;
        if (node->isLeaf())
            return node->pane == pane ? node : nullptr;
        if (Node *found = visit(node->first.get()))
            return found;
        return visit(node->second.get());
    };
    return visit(m_root.get());
}

bool SplitTree::split(int pane, SplitDirection direction, int newPane)
{
    Node *leaf = find(pane);
    if (!leaf)
        return false;
    Q_ASSERT(!contains(newPane));

    // The leaf becomes a split holding the old pane and the new one.
    auto oldLeaf = std::make_unique<Node>();
    oldLeaf->pane = pane;
    auto added = std::make_unique<Node>();
    added->pane = newPane;
    leaf->pane = -1;
    leaf->vertical = isHorizontal(direction);
    leaf->ratio = 0.5;
    leaf->dividerId = m_nextDividerId++;
    const bool after = direction == SplitDirection::Right || direction == SplitDirection::Down;
    leaf->first = after ? std::move(oldLeaf) : std::move(added);
    leaf->second = after ? std::move(added) : std::move(oldLeaf);
    leaf->first->parent = leaf;
    leaf->second->parent = leaf;
    m_zoomed = -1;
    return true;
}

bool SplitTree::remove(int pane)
{
    Node *leaf = find(pane);
    if (!leaf || leaf == m_root.get())
        return false;
    Node *parent = leaf->parent;
    std::unique_ptr<Node> sibling = parent->first.get() == leaf ? std::move(parent->second)
                                                                : std::move(parent->first);
    // The sibling replaces the parent in place, keeping the grandparent's
    // ownership and the sibling's subtree intact.
    Node *grandparent = parent->parent;
    sibling->parent = grandparent;
    if (!grandparent) {
        m_root = std::move(sibling);
    } else if (grandparent->first.get() == parent) {
        grandparent->first = std::move(sibling);
    } else {
        grandparent->second = std::move(sibling);
    }
    if (m_zoomed == pane)
        m_zoomed = -1;
    return true;
}

bool SplitTree::contains(int pane) const
{
    return find(pane) != nullptr;
}

bool SplitTree::isEmpty() const
{
    return !m_root;
}

QList<int> SplitTree::panes() const
{
    QList<int> result;
    std::function<void(const Node *)> visit = [&](const Node *node) {
        if (node->isLeaf()) {
            result.append(node->pane);
            return;
        }
        visit(node->first.get());
        visit(node->second.get());
    };
    visit(m_root.get());
    return result;
}

QHash<int, QRectF> SplitTree::layout(const QRectF &area, qreal gap) const
{
    if (m_zoomed >= 0)
        return {{m_zoomed, area}};
    return unzoomedLayout(area, gap);
}

QHash<int, QRectF> SplitTree::unzoomedLayout(const QRectF &area, qreal gap) const
{
    QHash<int, QRectF> result;
    std::function<void(const Node *, const QRectF &)> visit = [&](const Node *node, const QRectF &rect) {
        if (node->isLeaf()) {
            result.insert(node->pane, rect);
            return;
        }
        const auto [a, b] = node->childRects(rect, gap);
        visit(node->first.get(), a);
        visit(node->second.get(), b);
    };
    visit(m_root.get(), area);
    return result;
}

QList<SplitTree::Divider> SplitTree::dividers(const QRectF &area, qreal gap) const
{
    QList<Divider> result;
    if (m_zoomed >= 0)
        return result;
    std::function<void(const Node *, const QRectF &)> visit = [&](const Node *node, const QRectF &rect) {
        if (node->isLeaf())
            return;
        const auto [a, b] = node->childRects(rect, gap);
        const QRectF line = node->vertical
            ? QRectF(a.right(), rect.y(), gap, rect.height())
            : QRectF(rect.x(), a.bottom(), rect.width(), gap);
        result.append({node->dividerId, line, node->vertical, rect});
        visit(node->first.get(), a);
        visit(node->second.get(), b);
    };
    visit(m_root.get(), area);
    return result;
}

int SplitTree::next(int pane) const
{
    const QList<int> order = panes();
    const qsizetype index = order.indexOf(pane);
    if (index < 0)
        return -1;
    return order.at((index + 1) % order.size());
}

int SplitTree::previous(int pane) const
{
    const QList<int> order = panes();
    const qsizetype index = order.indexOf(pane);
    if (index < 0)
        return -1;
    return order.at((index + order.size() - 1) % order.size());
}

int SplitTree::neighbor(int pane, SplitDirection direction, const QRectF &area, qreal gap) const
{
    // Zoom hides the other panes, so navigation uses the unzoomed layout.
    const QHash<int, QRectF> rects = unzoomedLayout(area, gap);

    const auto from = rects.constFind(pane);
    if (from == rects.constEnd())
        return -1;
    const QRectF origin = *from;
    int best = -1;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    qreal bestOverlap = -1;
    // Visit in reading order so ties go to the top or left candidate.
    for (int candidate : panes()) {
        if (candidate == pane)
            continue;
        const QRectF rect = rects.value(candidate);
        qreal distance = 0;
        qreal overlap = 0;
        switch (direction) {
        case SplitDirection::Left:
            distance = origin.left() - rect.right();
            overlap = std::min(origin.bottom(), rect.bottom()) - std::max(origin.top(), rect.top());
            break;
        case SplitDirection::Right:
            distance = rect.left() - origin.right();
            overlap = std::min(origin.bottom(), rect.bottom()) - std::max(origin.top(), rect.top());
            break;
        case SplitDirection::Up:
            distance = origin.top() - rect.bottom();
            overlap = std::min(origin.right(), rect.right()) - std::max(origin.left(), rect.left());
            break;
        case SplitDirection::Down:
            distance = rect.top() - origin.bottom();
            overlap = std::min(origin.right(), rect.right()) - std::max(origin.left(), rect.left());
            break;
        }
        if (distance < 0 || overlap <= 0)
            continue;
        if (distance < bestDistance || (qFuzzyCompare(distance, bestDistance) && overlap > bestOverlap)) {
            best = candidate;
            bestDistance = distance;
            bestOverlap = overlap;
        }
    }
    return best;
}

bool SplitTree::resize(int pane, SplitDirection direction, qreal pixels, const QRectF &area, qreal gap)
{
    Node *node = find(pane);
    if (!node)
        return false;
    const bool vertical = isHorizontal(direction);
    Node *split = node->parent;
    while (split && split->vertical != vertical)
        split = split->parent;
    if (!split)
        return false;

    // Find the split's rectangle by walking down from the root.
    QRectF rect = area;
    std::function<bool(const Node *, const QRectF &)> locate = [&](const Node *current, const QRectF &r) {
        if (current == split) {
            rect = r;
            return true;
        }
        if (current->isLeaf())
            return false;
        const auto [a, b] = current->childRects(r, gap);
        return locate(current->first.get(), a) || locate(current->second.get(), b);
    };
    locate(m_root.get(), area);

    const qreal span = (vertical ? rect.width() : rect.height()) - gap;
    if (span <= 0)
        return false;
    const qreal sign = (direction == SplitDirection::Left || direction == SplitDirection::Up) ? -1 : 1;
    split->ratio = std::clamp(split->ratio + sign * pixels / span, minRatio, maxRatio);
    return true;
}

bool SplitTree::setDividerRatio(int divider, qreal ratio)
{
    std::function<Node *(Node *)> visit = [&](Node *node) -> Node * {
        if (node->isLeaf())
            return nullptr;
        if (node->dividerId == divider)
            return node;
        if (Node *found = visit(node->first.get()))
            return found;
        return visit(node->second.get());
    };
    Node *split = visit(m_root.get());
    if (!split)
        return false;
    split->ratio = std::clamp(ratio, minRatio, maxRatio);
    return true;
}

void SplitTree::equalize()
{
    // Weight each side by the panes it holds along the split's axis, so
    // three panes in a row get a third each, like Ghostty.
    std::function<int(const Node *, bool)> countAlong = [&](const Node *node, bool vertical) -> int {
        if (node->isLeaf())
            return 1;
        if (node->vertical == vertical)
            return countAlong(node->first.get(), vertical) + countAlong(node->second.get(), vertical);
        return std::max(countAlong(node->first.get(), vertical), countAlong(node->second.get(), vertical));
    };
    std::function<void(Node *)> visit = [&](Node *node) {
        if (node->isLeaf())
            return;
        const int a = countAlong(node->first.get(), node->vertical);
        const int b = countAlong(node->second.get(), node->vertical);
        node->ratio = qreal(a) / qreal(a + b);
        visit(node->first.get());
        visit(node->second.get());
    };
    visit(m_root.get());
}

void SplitTree::setZoomed(int pane)
{
    Q_ASSERT(pane < 0 || contains(pane));
    m_zoomed = pane;
}
