#include "monitorlayout.h"

#include <climits>
#include <cstdlib>

namespace Layout {

// QRect's right() and bottom() are one short of the edge, so edges are
// worked out from x + width here.
struct Box {
    int x, y, w, h;
    int right() const { return x + w; }
    int bottom() const { return y + h; }
};

static bool overlaps(const Box &a, const Box &b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

// Sharing a stretch of edge, not just a corner.
static bool touches(const Box &a, const Box &b) {
    const bool side = (a.right() == b.x || b.right() == a.x) && a.y < b.bottom() && b.y < a.bottom();
    const bool topBottom = (a.bottom() == b.y || b.bottom() == a.y) && a.x < b.right() && b.x < a.right();
    return side || topBottom;
}

static bool isFree(const Box &box, const QList<Box> &others) {
    for (const Box &o : others)
        if (overlaps(box, o))
            return false;
    return true;
}

// The candidate nearest `value` within `snap`, or `value`.
static int snapTo(int value, const QList<int> &candidates, int snap) {
    int best = value, distance = snap + 1;
    for (int c : candidates)
        if (std::abs(c - value) < distance) {
            distance = std::abs(c - value);
            best = c;
        }
    return best;
}

static QList<int> edgesX(const Box &r, const QList<Box> &others) {
    QList<int> out;
    for (const Box &o : others)
        out << o.x - r.w << o.right() << o.x << o.right() - r.w << o.x + (o.w - r.w) / 2;
    return out;
}

static QList<int> edgesY(const Box &r, const QList<Box> &others) {
    QList<int> out;
    for (const Box &o : others)
        out << o.y - r.h << o.bottom() << o.y << o.bottom() - r.h << o.y + (o.h - r.h) / 2;
    return out;
}

static qint64 distance(const Box &a, const Box &b) {
    const qint64 dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

QList<QPoint> place(const QList<QRect> &rects, int moved, QPoint proposed, int snap) {
    QList<Box> boxes;
    for (const QRect &r : rects)
        boxes << Box{r.x(), r.y(), qMax(1, r.width()), qMax(1, r.height())};
    if (moved >= 0 && moved < boxes.size()) {
        QList<Box> others;
        for (int i = 0; i < boxes.size(); ++i)
            if (i != moved)
                others << boxes[i];
        Box r = boxes[moved];
        r.x = proposed.x();
        r.y = proposed.y();

        if (!others.isEmpty()) {
            // 1. Line up with nearby edges.
            r.x = snapTo(r.x, edgesX(r, others), snap);
            r.y = snapTo(r.y, edgesY(r, others), snap);

            // 2 and 3. Out of any overlap, and against an edge: the nearest
            // spot along any side of any other display that is free.
            bool touching = false;
            for (const Box &o : others)
                touching = touching || touches(r, o);
            if (!isFree(r, others) || !touching) {
                Box best = r;
                qint64 bestDistance = LLONG_MAX;
                auto consider = [&](Box c) {
                    if (isFree(c, others) && distance(c, r) < bestDistance) {
                        best = c;
                        bestDistance = distance(c, r);
                    }
                };
                for (const Box &o : others) {
                    // Slide along the side, keeping at least one unit of
                    // edge shared, then line up with that side's ends or
                    // middle when close.
                    const int y = qBound(o.y - r.h + 1, r.y, o.bottom() - 1);
                    const int x = qBound(o.x - r.w + 1, r.x, o.right() - 1);
                    const int ys = snapTo(y, {o.y, o.bottom() - r.h, o.y + (o.h - r.h) / 2}, snap);
                    const int xs = snapTo(x, {o.x, o.right() - r.w, o.x + (o.w - r.w) / 2}, snap);
                    consider(Box{o.x - r.w, ys, r.w, r.h});
                    consider(Box{o.right(), ys, r.w, r.h});
                    consider(Box{xs, o.y - r.h, r.w, r.h});
                    consider(Box{xs, o.bottom(), r.w, r.h});
                }
                if (bestDistance == LLONG_MAX) {
                    // Nowhere free beside any one display: past the right of
                    // them all.
                    int right = INT_MIN, top = INT_MAX;
                    for (const Box &o : others) {
                        right = qMax(right, o.right());
                        top = qMin(top, o.y);
                    }
                    best = Box{right, top, r.w, r.h};
                }
                r = best;
            }
        }
        boxes[moved] = r;
    }

    // 4. Start at 0,0.
    int left = INT_MAX, top = INT_MAX;
    for (const Box &b : boxes) {
        left = qMin(left, b.x);
        top = qMin(top, b.y);
    }
    QList<QPoint> out;
    for (const Box &b : boxes)
        out << QPoint(b.x - left, b.y - top);
    return out;
}

}
