#pragma once

#include <QList>
#include <QString>
#include <QVector>

// A parsed unified diff, flattened into display lines the way
// a classic one-pane diff view shows them.
struct DiffSpan {
    int start = 0; // character offset
    int length = 0;
};

struct DiffLine {
    enum State { Normal, Added, Removed, Header, Empty };
    State state = Normal;
    int oldNumber = -1; // 1-based, -1 if not applicable
    int newNumber = -1;
    QString text;       // without trailing newline, tabs expanded on paint
    QVector<DiffSpan> inline_; // intra-line changed ranges
    bool noNewline = false;
};

struct DiffDocument {
    QList<DiffLine> lines;
    int added = 0;
    int removed = 0;
    bool binary = false;
    QString message; // shown instead of lines when not empty

    // Indices of first lines of each change block, for next/prev navigation.
    QVector<int> blockStarts;
};

class DiffModel
{
public:
    static DiffDocument parse(const QString &unified);
    static void computeInlineDiffs(DiffDocument &doc);
};
