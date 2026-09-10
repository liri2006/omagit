#pragma once

#include "DiffModel.h"

#include <QString>

// A small hand-rolled tokeniser for the diff view.
//
// It is not a parser: it recognises comments, strings, numbers, keywords and
// a few per-language extras (preprocessor lines, decorators, attributes) with
// one character loop per line, and carries block-comment / triple-quote state
// from line to line. The diff has two independent versions of the file in it,
// so the pass tracks one state for the old side (context + removed lines) and
// one for the new side (context + added lines): a `/*` that a change opens on
// one side must not colour the other side's lines.
enum class Language {
    None,
    Cpp,
    CSharp,
    Java,
    Kotlin,
    Dart,
    JavaScript,
    TypeScript,
    Python,
    Rust,
    Go,
    Ruby,
    Lua,
    Shell,
    Sql,
    Json,
    Yaml,
    Toml,
    Ini,
    Markdown,
    Css,
    Html,
    Make,
    QMake,
    CMake,
    Dockerfile,
};

class SyntaxHighlighter
{
public:
    // Language of `path` from its extension or basename; falls back to the
    // `#!` line of the document's first content line when the name says nothing.
    static Language languageFor(const QString &path, const DiffDocument *docForShebang = nullptr);
    static QString displayName(Language lang);

    // Fills DiffLine::syntax for every content line. O(total characters).
    static void highlight(DiffDocument &doc, Language lang);
    static void clear(DiffDocument &doc);
};
