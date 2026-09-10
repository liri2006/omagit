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

// What a syntax span means; the colours live in OmarchyTheme::syntaxColor().
enum class TokenKind {
    Keyword,
    Type,          // built-in types and well-known library names
    String,        // string and character literals
    Comment,
    Number,
    Preprocessor,  // #include, decorators, attributes, [sections]
    Function,      // name of a call / definition
};

// How many kinds there are, for arrays indexed by one. It is not an enumerator
// because OmarchyTheme::syntaxColor() switches over every kind and a sentinel
// would only add a case that can never happen.
constexpr int kTokenKindCount = int(TokenKind::Function) + 1;

// A coloured range, in the same raw-character offsets as DiffSpan.
struct SyntaxSpan {
    int start = 0;
    int length = 0;
    TokenKind kind = TokenKind::Keyword;
};

struct DiffLine {
    enum State { Normal, Added, Removed, Header };
    State state = Normal;
    int oldNumber = -1; // 1-based, -1 if not applicable
    int newNumber = -1;
    QString text;       // without trailing newline, tabs expanded on paint
    QVector<DiffSpan> inline_; // intra-line changed ranges
    QVector<SyntaxSpan> syntax; // syntax colouring, filled in by SyntaxHighlighter
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

    // Walks the change blocks of a diff: a run of removed lines and the run of
    // added lines that follows it, either of which may be empty. `fn` is called
    // with the half-open ranges [r0, r1) and [a0, a1); context and header lines
    // between blocks are skipped. The inline diff and the two-pane row layout
    // pair the two sides up the same way, so they share this walk.
    template <typename Fn>
    static void forEachChangeBlock(const QList<DiffLine> &lines, Fn fn)
    {
        const int n = lines.size();
        int i = 0;
        while (i < n) {
            const DiffLine::State state = lines.at(i).state;
            if (state != DiffLine::Removed && state != DiffLine::Added) {
                ++i;
                continue;
            }
            const int r0 = i;
            while (i < n && lines.at(i).state == DiffLine::Removed)
                ++i;
            const int r1 = i;
            const int a0 = i;
            while (i < n && lines.at(i).state == DiffLine::Added)
                ++i;
            fn(r0, r1, a0, i);
        }
    }
};
