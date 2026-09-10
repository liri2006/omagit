#include "DiffModel.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

// Guard rails for the inline (intra-line) diff: the LCS table is O(n·m), so a
// minified or generated line is not worth the time, and once nearly everything
// on both sides changed a full-line highlight reads better than a shredded one.
static constexpr qint64 kMaxLcsCells = 4'000'000;
static constexpr int kMaxInlineChars = 4000;
static constexpr double kFullLineCoverage = 0.85;

DiffDocument DiffModel::parse(const QString &unified)
{
    DiffDocument doc;
    if (unified.contains(QLatin1String("Binary files")) && !unified.contains(QLatin1String("\n@@"))) {
        doc.binary = true;
        // MainWindow shows its own wording; this is the fallback for any other
        // caller, so it still goes through the translation catalogue.
        doc.message = QCoreApplication::translate("DiffModel", "Binary file — no textual diff available.");
        return doc;
    }

    static const QRegularExpression hunkRe(QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@"));
    const QStringList raw = unified.split(QLatin1Char('\n'));
    int oldNo = 0, newNo = 0;
    bool inHunk = false;
    bool firstHunk = true;

    for (const QString &line : raw) {
        const auto m = hunkRe.match(line);
        if (m.hasMatch()) {
            oldNo = m.captured(1).toInt();
            newNo = m.captured(3).toInt();
            inHunk = true;
            if (!firstHunk) {
                DiffLine h;
                h.state = DiffLine::Header;
                h.text = line;
                doc.lines.append(h);
            }
            firstHunk = false;
            continue;
        }
        if (!inHunk)
            continue;
        if (line.isEmpty())
            continue; // trailing split artefact
        const QChar tag = line.at(0);
        if (tag == QLatin1Char('\\')) {
            if (!doc.lines.isEmpty())
                doc.lines.last().noNewline = true;
            continue;
        }
        DiffLine l;
        l.text = line.mid(1);
        if (l.text.endsWith(QLatin1Char('\r')))
            l.text.chop(1);
        if (tag == QLatin1Char('+')) {
            l.state = DiffLine::Added;
            l.newNumber = newNo++;
            ++doc.added;
        } else if (tag == QLatin1Char('-')) {
            l.state = DiffLine::Removed;
            l.oldNumber = oldNo++;
            ++doc.removed;
        } else {
            l.state = DiffLine::Normal;
            l.oldNumber = oldNo++;
            l.newNumber = newNo++;
        }
        doc.lines.append(l);
    }

    // Change blocks: runs of non-normal lines.
    bool inBlock = false;
    for (int i = 0; i < doc.lines.size(); ++i) {
        const bool changed = doc.lines[i].state == DiffLine::Added || doc.lines[i].state == DiffLine::Removed;
        if (changed && !inBlock)
            doc.blockStarts.append(i);
        inBlock = changed;
    }

    computeInlineDiffs(doc);
    return doc;
}

// Tokenise into words / whitespace runs / single punctuation so the inline
// highlight lands on readable units rather than individual characters.
struct Token {
    int start;
    int length;
    QStringView text;
};

static QVector<Token> tokenize(const QString &s)
{
    QVector<Token> out;
    int i = 0;
    const int n = s.size();
    while (i < n) {
        int j = i + 1;
        const QChar c = s.at(i);
        if (c.isLetterOrNumber() || c == QLatin1Char('_')) {
            while (j < n && (s.at(j).isLetterOrNumber() || s.at(j) == QLatin1Char('_')))
                ++j;
        } else if (c.isSpace()) {
            while (j < n && s.at(j).isSpace())
                ++j;
        }
        out.append({i, j - i, QStringView(s).mid(i, j - i)});
        i = j;
    }
    return out;
}

// Classic LCS over tokens; marks tokens that are not part of the common
// subsequence as changed on each side. `dp` is the caller's scratch table,
// handed in so a file full of changed lines allocates it once.
static void inlineDiff(const QString &a, const QString &b, QVector<DiffSpan> &spansA,
                       QVector<DiffSpan> &spansB, QVector<int> &dp)
{
    const QVector<Token> ta = tokenize(a), tb = tokenize(b);
    const int n = ta.size(), m = tb.size();
    if (n == 0 || m == 0 || qint64(n) * m > kMaxLcsCells) {
        if (n)
            spansA.append({0, int(a.size())});
        if (m)
            spansB.append({0, int(b.size())});
        return;
    }
    dp.resize((n + 1) * (m + 1));
    dp.fill(0);
    auto at = [&](int i, int j) -> int & { return dp[i * (m + 1) + j]; };
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            at(i, j) = ta[i].text == tb[j].text ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));

    QVector<bool> keepA(n, false), keepB(m, false);
    int i = 0, j = 0;
    while (i < n && j < m) {
        if (ta[i].text == tb[j].text) {
            keepA[i] = keepB[j] = true;
            ++i;
            ++j;
        } else if (at(i + 1, j) >= at(i, j + 1)) {
            ++i;
        } else {
            ++j;
        }
    }
    auto collect = [](const QVector<Token> &toks, const QVector<bool> &keep, QVector<DiffSpan> &spans) {
        int k = 0;
        while (k < toks.size()) {
            if (keep[k]) {
                ++k;
                continue;
            }
            int start = toks[k].start, end = start;
            while (k < toks.size() && !keep[k]) {
                end = toks[k].start + toks[k].length;
                ++k;
            }
            spans.append({start, end - start});
        }
    };
    collect(ta, keepA, spansA);
    collect(tb, keepB, spansB);

    // If nearly everything changed, a full-line highlight reads better.
    auto coverage = [](const QVector<DiffSpan> &spans, int len) {
        int c = 0;
        for (const DiffSpan &s : spans)
            c += s.length;
        return len ? double(c) / len : 0.0;
    };
    if (coverage(spansA, a.size()) > kFullLineCoverage && coverage(spansB, b.size()) > kFullLineCoverage) {
        spansA = {{0, int(a.size())}};
        spansB = {{0, int(b.size())}};
    }
}

void DiffModel::computeInlineDiffs(DiffDocument &doc)
{
    // Pair the i-th removed line of a block with the i-th added line, the
    // same way a classic one-pane diff view does.
    QVector<int> dp;
    forEachChangeBlock(doc.lines, [&](int r0, int r1, int a0, int a1) {
        const int pairs = std::min(r1 - r0, a1 - a0);
        for (int k = 0; k < pairs; ++k) {
            DiffLine &rem = doc.lines[r0 + k];
            DiffLine &add = doc.lines[a0 + k];
            if (rem.text.size() > kMaxInlineChars || add.text.size() > kMaxInlineChars)
                continue;
            inlineDiff(rem.text, add.text, rem.inline_, add.inline_, dp);
        }
    });
}
