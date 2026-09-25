#pragma once

#include "GitRepo.h"

#include <QPoint>

class QPainter;

// The chip a branch, a remote branch or a tag wears beside a commit (kit.js
// refChip()): 16 px tall, its label in the bold 10 px caption with 4 px on
// either side. The checked-out branch (or a detached HEAD) is a solid accent
// chip; any other local branch is tinted in the accent, a remote one in
// magenta, a tag in yellow. The commit list's rows and the details card
// under it measure and paint their chips here, so the two never disagree.
int refChipWidth(const RefLabel &label);
int refChipHeight();
// The chip with its top left corner at `topLeft`.
void paintRefChip(QPainter *painter, const QPoint &topLeft, const RefLabel &label);
