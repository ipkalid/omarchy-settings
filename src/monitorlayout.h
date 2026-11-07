#pragma once

#include <QList>
#include <QPoint>
#include <QRect>

// Where displays go when one is dragged in the Displays pane's arrangement.
// Rects are in Hyprland's layout coordinates: position, and the size the
// display takes up (its mode divided by its scale, turned if rotated).
namespace Layout {

// The position of every display after display `moved` is dropped at
// `proposed`: its edges snap to the others' edges within `snap`, it never
// overlaps another, it always touches one edge to edge (so the cursor can
// cross between them), and the whole layout starts at 0,0.
QList<QPoint> place(const QList<QRect> &rects, int moved, QPoint proposed, int snap);

}
