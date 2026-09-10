#include "SyntaxHighlighter.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

namespace {

// ---------------------------------------------------------------- word lists

QSet<QString> setOf(const char *words)
{
    QSet<QString> out;
    const QStringList list = QString::fromLatin1(words).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &w : list)
        out.insert(w);
    return out;
}

// Everything is a function-local static so the tables are built once, on the
// first line of the first file of that language, and never again.
#define WORDS(name, words)                              \
    const QSet<QString> &name()                         \
    {                                                   \
        static const QSet<QString> s = setOf(words);    \
        return s;                                       \
    }

WORDS(cppKeywords,
      "alignas alignof and and_eq asm bitand bitor break case catch class compl concept const "
      "consteval constexpr constinit const_cast continue co_await co_return co_yield decltype "
      "default delete do dynamic_cast else enum explicit export extern false final for friend goto "
      "if inline mutable namespace new noexcept not not_eq nullptr operator or or_eq override "
      "private protected public register reinterpret_cast requires return sizeof static "
      "static_assert static_cast struct switch template this thread_local throw true try typedef "
      "typeid typename union using virtual volatile while xor xor_eq")
WORDS(cppTypes,
      "auto bool char char8_t char16_t char32_t double float int long short signed unsigned void "
      "wchar_t size_t ssize_t ptrdiff_t intptr_t uintptr_t int8_t int16_t int32_t int64_t uint8_t "
      "uint16_t uint32_t uint64_t std string wstring string_view vector map unordered_map set "
      "unordered_set pair tuple array deque list optional variant function shared_ptr unique_ptr "
      "weak_ptr")

WORDS(pythonKeywords,
      "and as assert async await break class continue def del elif else except finally for from "
      "global if import in is lambda match nonlocal not or pass raise return try while with yield")
WORDS(pythonTypes,
      "True False None self cls bool bytes bytearray complex dict float frozenset int list object "
      "set str tuple type abs all any enumerate isinstance issubclass len max min open print range "
      "repr reversed round sorted sum super zip Exception ValueError TypeError KeyError IndexError "
      "RuntimeError NotImplementedError")

WORDS(jsKeywords,
      "abstract as async await break case catch class const constructor continue debugger declare "
      "default delete do else enum export extends finally for from function get if implements "
      "import in infer instanceof interface is keyof let namespace new of package private "
      "protected public readonly require return satisfies set static super switch throw try type "
      "typeof var void while with yield")
WORDS(jsTypes,
      "any bigint boolean never null number object string symbol this undefined unknown true false "
      "NaN Infinity Array Boolean Date Error JSON Map Math Number Object Promise RegExp Set String "
      "Symbol WeakMap WeakSet console document globalThis process window")

WORDS(rustKeywords,
      "as async await break const continue crate dyn else enum extern false fn for if impl in let "
      "loop match mod move mut pub ref return self Self static struct super trait true type unsafe "
      "use where while")
WORDS(rustTypes,
      "bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize String Vec Option "
      "Some None Result Ok Err Box Rc Arc RefCell Cell Cow HashMap HashSet BTreeMap BTreeSet")

WORDS(goKeywords,
      "break case chan const continue default defer else fallthrough for func go goto if import "
      "interface map package range return select struct switch type var")
WORDS(goTypes,
      "any bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune "
      "string uint uint8 uint16 uint32 uint64 uintptr true false nil iota append cap close copy "
      "delete len make new panic print println recover")

WORDS(shellKeywords,
      "if then else elif fi case esac for while until do done in function select time coproc "
      "return break continue exit export local readonly declare typeset unset shift source alias "
      "eval exec set trap")
WORDS(shellTypes,
      "echo printf cd pwd read test true false let mapfile getopts command builtin type hash umask "
      "wait kill jobs bg fg pushd popd dirs")

WORDS(javaKeywords,
      "abstract assert break case catch class const continue default do else enum extends final "
      "finally for goto if implements import instanceof interface native new package permits "
      "private protected public record return sealed static strictfp super switch synchronized "
      "this throw throws transient try var volatile while yield")
WORDS(javaTypes,
      "boolean byte char double float int long short void true false null String Object Integer "
      "Double Float Long Short Boolean Character List Map Set Collection ArrayList HashMap HashSet "
      "Optional Stream System")

WORDS(kotlinKeywords,
      "as break by catch class companion const constructor continue crossinline data do dynamic "
      "else enum expect external final finally for fun get if import in infix init inline inner "
      "interface internal is lateinit noinline object open operator out override package private "
      "protected public reified return sealed set super suspend tailrec this throw try typealias "
      "val var vararg when where while")
WORDS(kotlinTypes,
      "Any Boolean Byte Char Double Float Int Long Nothing Short String Unit Array List Map Set "
      "MutableList MutableMap MutableSet Pair Triple true false null it")

WORDS(dartKeywords,
      "abstract as assert async await base break case catch class const continue covariant default "
      "deferred do dynamic else enum export extends extension external factory false final finally "
      "for get hide if implements import in interface is late library mixin new null on operator "
      "part required rethrow return sealed set show static super switch sync this throw true try "
      "typedef var void when while with yield")
WORDS(dartTypes,
      "int double num bool String List Map Set Iterable Future Stream Object Null Never Function "
      "Symbol Type Duration DateTime Uri BigInt Runes Record Widget BuildContext State StatelessWidget "
      "StatefulWidget print identical")

WORDS(csharpKeywords,
      "abstract as async await base break case catch checked class const continue default delegate "
      "do else enum event explicit extern finally fixed for foreach get goto if implicit in "
      "interface internal is lock namespace new operator out override params partial private "
      "protected public readonly record ref return sealed set sizeof stackalloc static struct "
      "switch this throw try typeof unchecked unsafe using value virtual volatile while yield")
WORDS(csharpTypes,
      "bool byte char decimal double dynamic float int long nint nuint object sbyte short string "
      "uint ulong ushort var void true false null List Dictionary Task String Int32 Console "
      "Exception IEnumerable")

WORDS(rubyKeywords,
      "BEGIN END alias and begin break case class def defined? do else elsif end ensure false for "
      "if in module next nil not or redo rescue retry return self super then true undef unless "
      "until when while yield require require_relative include extend attr_accessor attr_reader "
      "attr_writer")
WORDS(rubyTypes,
      "Array Class Comparable Enumerable Exception Float Hash Integer Module Numeric Proc Range "
      "Regexp String Struct Symbol Time puts print p lambda new")

WORDS(luaKeywords,
      "and break do else elseif end false for function goto if in local nil not or repeat return "
      "then true until while")
WORDS(luaTypes,
      "assert collectgarbage coroutine dofile error getmetatable io ipairs load loadstring math "
      "next os pairs pcall print rawequal rawget rawlen rawset require select self setmetatable "
      "string table tonumber tostring type unpack xpcall")

WORDS(sqlKeywords,
      "add all alter and as asc begin between by cascade case check column commit constraint "
      "create cross default delete desc distinct drop else end exists foreign from full group "
      "having if in index inner insert into is join key left like limit not null offset on or "
      "order outer primary references replace returning right rollback select set table then "
      "transaction union unique update using values view when where with")
WORDS(sqlTypes,
      "bigint blob boolean bytea char date datetime decimal double float int integer json jsonb "
      "money numeric precision real serial smallint text time timestamp uuid varchar")

WORDS(jsonKeywords, "true false null")
WORDS(yamlKeywords, "true false null yes no on off True False Null Yes No On Off ~")
WORDS(tomlKeywords, "true false")

WORDS(cmakeKeywords,
      "add_compile_definitions add_custom_command add_custom_target add_definitions add_dependencies "
      "add_executable add_library add_subdirectory add_test break cmake_minimum_required configure_file "
      "continue else elseif endforeach endfunction endif endmacro endwhile execute_process find_library "
      "find_package find_path find_program foreach function get_filename_component get_target_property "
      "if include install list macro math message option project return set set_target_properties "
      "string target_compile_definitions target_compile_options target_include_directories "
      "target_link_libraries target_sources unset while")
WORDS(cmakeTypes,
      "AND OR NOT EQUAL STREQUAL MATCHES EXISTS DEFINED PRIVATE PUBLIC INTERFACE REQUIRED QUIET "
      "COMPONENTS TARGET DESTINATION CACHE FORCE STATIC SHARED MODULE ON OFF TRUE FALSE")

WORDS(makeKeywords,
      "define else endef endif export ifdef ifeq ifndef ifneq include override sinclude unexport "
      "vpath -include")
WORDS(makeTypes, "MAKE MAKEFLAGS CC CXX CFLAGS CXXFLAGS LDFLAGS SHELL PHONY")

WORDS(qmakeKeywords,
      "TEMPLATE TARGET CONFIG QT SOURCES HEADERS FORMS RESOURCES DEFINES INCLUDEPATH LIBS DEPENDPATH "
      "DESTDIR OBJECTS_DIR MOC_DIR UI_DIR RCC_DIR VERSION SUBDIRS PKGCONFIG DISTFILES TRANSLATIONS "
      "QMAKE_CXXFLAGS QMAKE_CFLAGS QMAKE_LFLAGS QMAKE_TARGET INSTALLS")
WORDS(qmakeTypes,
      "android contains count defineReplace defineTest else equals error eval exists for greaterThan "
      "include isEmpty lessThan macx message requires return unix warning win32")

WORDS(dockerKeywords,
      "add arg cmd copy entrypoint env expose from healthcheck label maintainer onbuild run shell "
      "stopsignal user volume workdir as")

#undef WORDS

// --------------------------------------------------------------- language spec

// Multi-line constructs the scanner may be sitting inside when a line starts.
enum Block {
    NoBlock,
    CBlock,      // /* ... */
    PyDouble,    // """ ... """
    PySingle,    // ''' ... '''
    LuaBlock,    // --[[ ... ]]
    XmlComment,  // <!-- ... -->
};

struct LangSpec {
    const QSet<QString> *keywords = nullptr;
    const QSet<QString> *types = nullptr;
    bool foldCase = false;      // keyword lookup is case-insensitive (SQL, CMake, Dockerfile)

    // Comments
    const char *lineComment = nullptr;   // "//", "--", "%%" …
    bool hashComment = false;            // '#' to end of line
    bool semicolonComment = false;       // ';' to end of line (ini)
    bool cBlockComment = false;          // /* … */
    bool luaLongComment = false;         // --[[ … ]]
    bool xmlComment = false;             // <!-- … -->

    // Literals
    bool doubleQuote = true;
    bool singleQuoteString = false;      // '…' is a string
    bool charLiteral = false;            // '…' is a char literal (and 'a may be a lifetime)
    bool backtick = false;               // `…` template / command string
    bool pyTriple = false;               // """…""" and '''…'''
    bool rustRaw = false;                // r#"…"#
    bool numbers = true;

    // Extras
    bool cPreproc = false;               // #include / #define at the start of a line
    bool atDecorator = false;            // @foo → Preprocessor
    bool hashAttribute = false;          // Rust #[…]
    bool dollarVar = false;              // $VAR / ${…} → Type
    bool hashColor = false;              // CSS #1a2b3c → Number
    bool sectionHeader = false;          // [section] at the start of a line → Preprocessor
    bool xmlTags = false;                // <tag …> → Keyword + attributes
    bool markdown = false;               // headings / quotes / `code`
    bool functionCalls = false;          // name( → Function
    bool keyBeforeColon = false;         // name: → colonKind
    bool keyBeforeEquals = false;        // name= → Type
    TokenKind colonKind = TokenKind::Type;
};

const LangSpec &specFor(Language lang)
{
    static LangSpec none;

    static const LangSpec cpp = [] {
        LangSpec s;
        s.keywords = &cppKeywords();
        s.types = &cppTypes();
        s.lineComment = "//";
        s.cBlockComment = true;
        s.charLiteral = true;
        s.cPreproc = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec python = [] {
        LangSpec s;
        s.keywords = &pythonKeywords();
        s.types = &pythonTypes();
        s.hashComment = true;
        s.singleQuoteString = true;
        s.pyTriple = true;
        s.atDecorator = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec js = [] {
        LangSpec s;
        s.keywords = &jsKeywords();
        s.types = &jsTypes();
        s.lineComment = "//";
        s.cBlockComment = true;
        s.singleQuoteString = true;
        s.backtick = true;
        s.atDecorator = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec rust = [] {
        LangSpec s;
        s.keywords = &rustKeywords();
        s.types = &rustTypes();
        s.lineComment = "//";
        s.cBlockComment = true;
        s.charLiteral = true;
        s.rustRaw = true;
        s.hashAttribute = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec go = [] {
        LangSpec s;
        s.keywords = &goKeywords();
        s.types = &goTypes();
        s.lineComment = "//";
        s.cBlockComment = true;
        s.charLiteral = true;
        s.backtick = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec shell = [] {
        LangSpec s;
        s.keywords = &shellKeywords();
        s.types = &shellTypes();
        s.hashComment = true;
        s.singleQuoteString = true;
        s.backtick = true;
        s.dollarVar = true;
        return s;
    }();

    static const LangSpec java = [] {
        LangSpec s;
        s.keywords = &javaKeywords();
        s.types = &javaTypes();
        s.lineComment = "//";
        s.cBlockComment = true;
        s.charLiteral = true;
        s.atDecorator = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec kotlin = [] {
        LangSpec s;
        s.keywords = &kotlinKeywords();
        s.types = &kotlinTypes();
        s.lineComment = "//";
        s.cBlockComment = true;
        s.charLiteral = true;
        s.atDecorator = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec dart = [] {
        LangSpec s;
        s.keywords = &dartKeywords();
        s.types = &dartTypes();
        s.lineComment = "//";
        s.cBlockComment = true;
        s.singleQuoteString = true;
        s.pyTriple = true;
        s.atDecorator = true;
        s.dollarVar = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec csharp = [] {
        LangSpec s;
        s.keywords = &csharpKeywords();
        s.types = &csharpTypes();
        s.lineComment = "//";
        s.cBlockComment = true;
        s.charLiteral = true;
        s.cPreproc = true;
        s.atDecorator = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec ruby = [] {
        LangSpec s;
        s.keywords = &rubyKeywords();
        s.types = &rubyTypes();
        s.hashComment = true;
        s.singleQuoteString = true;
        s.dollarVar = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec lua = [] {
        LangSpec s;
        s.keywords = &luaKeywords();
        s.types = &luaTypes();
        s.lineComment = "--";
        s.luaLongComment = true;
        s.singleQuoteString = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec sql = [] {
        LangSpec s;
        s.keywords = &sqlKeywords();
        s.types = &sqlTypes();
        s.foldCase = true;
        s.lineComment = "--";
        s.cBlockComment = true;
        s.singleQuoteString = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec json = [] {
        LangSpec s;
        s.keywords = &jsonKeywords();
        s.lineComment = "//"; // .jsonc, harmless in strict JSON
        return s;
    }();

    static const LangSpec yaml = [] {
        LangSpec s;
        s.keywords = &yamlKeywords();
        s.hashComment = true;
        s.singleQuoteString = true;
        s.keyBeforeColon = true;
        s.atDecorator = false;
        return s;
    }();

    static const LangSpec toml = [] {
        LangSpec s;
        s.keywords = &tomlKeywords();
        s.hashComment = true;
        s.singleQuoteString = true;
        s.sectionHeader = true;
        s.keyBeforeEquals = true;
        return s;
    }();

    static const LangSpec ini = [] {
        LangSpec s;
        s.keywords = &tomlKeywords();
        s.hashComment = true;
        s.semicolonComment = true;
        s.sectionHeader = true;
        s.keyBeforeEquals = true;
        return s;
    }();

    static const LangSpec markdown = [] {
        LangSpec s;
        s.markdown = true;
        s.numbers = false;
        s.doubleQuote = false;
        return s;
    }();

    static const LangSpec css = [] {
        LangSpec s;
        s.cBlockComment = true;
        s.singleQuoteString = true;
        s.atDecorator = true;
        s.hashColor = true;
        s.keyBeforeColon = true;
        return s;
    }();

    static const LangSpec html = [] {
        LangSpec s;
        s.xmlComment = true;
        s.xmlTags = true;
        s.singleQuoteString = true;
        s.keyBeforeEquals = true;
        s.numbers = false;
        return s;
    }();

    static const LangSpec make = [] {
        LangSpec s;
        s.keywords = &makeKeywords();
        s.types = &makeTypes();
        s.hashComment = true;
        s.singleQuoteString = true;
        s.dollarVar = true;
        s.keyBeforeColon = true;
        s.colonKind = TokenKind::Function;
        return s;
    }();

    static const LangSpec qmake = [] {
        LangSpec s;
        s.keywords = &qmakeKeywords();
        s.types = &qmakeTypes();
        s.hashComment = true;
        s.singleQuoteString = true;
        s.dollarVar = true;
        return s;
    }();

    static const LangSpec cmake = [] {
        LangSpec s;
        s.keywords = &cmakeKeywords();
        s.types = &cmakeTypes();
        s.foldCase = true;
        s.hashComment = true;
        s.dollarVar = true;
        s.functionCalls = true;
        return s;
    }();

    static const LangSpec docker = [] {
        LangSpec s;
        s.keywords = &dockerKeywords();
        s.foldCase = true;
        s.hashComment = true;
        s.singleQuoteString = true;
        s.dollarVar = true;
        return s;
    }();

    switch (lang) {
    case Language::Cpp: return cpp;
    case Language::CSharp: return csharp;
    case Language::Java: return java;
    case Language::Kotlin: return kotlin;
    case Language::Dart: return dart;
    case Language::JavaScript:
    case Language::TypeScript: return js;
    case Language::Python: return python;
    case Language::Rust: return rust;
    case Language::Go: return go;
    case Language::Ruby: return ruby;
    case Language::Lua: return lua;
    case Language::Shell: return shell;
    case Language::Sql: return sql;
    case Language::Json: return json;
    case Language::Yaml: return yaml;
    case Language::Toml: return toml;
    case Language::Ini: return ini;
    case Language::Markdown: return markdown;
    case Language::Css: return css;
    case Language::Html: return html;
    case Language::Make: return make;
    case Language::QMake: return qmake;
    case Language::CMake: return cmake;
    case Language::Dockerfile: return docker;
    case Language::None: break;
    }
    return none;
}

// -------------------------------------------------------------- the scanner

inline bool isIdentStart(QChar c) { return c.isLetter() || c == QLatin1Char('_'); }
inline bool isIdent(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

// Literal comparison that does not build a temporary QString per position.
inline bool matchAt(const QString &t, int i, const char *lit)
{
    const int n = t.size();
    for (int k = 0; lit[k]; ++k) {
        if (i + k >= n || t.at(i + k) != QLatin1Char(lit[k]))
            return false;
    }
    return true;
}

// r"", b'', f"", u8"", R"(…)", L"": a short prefix glued to a quote.
inline bool isStringPrefix(const QString &word)
{
    if (word.isEmpty() || word.size() > 3)
        return false;
    for (const QChar c : word) {
        if (!QLatin1String("rbufxRBUFXL8").contains(c))
            return false;
    }
    return true;
}

// End of a quoted run that starts at `start` (the quote itself). Returns the
// index just past the closing quote, or the line length when unterminated.
int endOfQuoted(const QString &t, int start, QChar quote, bool escapes)
{
    const int n = t.size();
    int i = start + 1;
    while (i < n) {
        const QChar c = t.at(i);
        if (escapes && c == QLatin1Char('\\')) {
            i += 2;
            continue;
        }
        if (c == quote)
            return i + 1;
        ++i;
    }
    return n;
}

int endOfNumber(const QString &t, int start)
{
    const int n = t.size();
    int i = start;
    if (t.at(i) == QLatin1Char('0') && i + 1 < n
        && (t.at(i + 1) == QLatin1Char('x') || t.at(i + 1) == QLatin1Char('X')
            || t.at(i + 1) == QLatin1Char('b') || t.at(i + 1) == QLatin1Char('B')
            || t.at(i + 1) == QLatin1Char('o') || t.at(i + 1) == QLatin1Char('O'))) {
        i += 2;
        while (i < n && (t.at(i).isLetterOrNumber() || t.at(i) == QLatin1Char('_')
                         || t.at(i) == QLatin1Char('\'')))
            ++i;
        return i;
    }
    bool seenDot = false;
    while (i < n) {
        const QChar c = t.at(i);
        if (c.isDigit() || c == QLatin1Char('_') || c == QLatin1Char('\'')) {
            ++i;
        } else if (c == QLatin1Char('.') && !seenDot && i + 1 < n && t.at(i + 1).isDigit()) {
            seenDot = true;
            ++i;
        } else if ((c == QLatin1Char('e') || c == QLatin1Char('E')) && i + 1 < n
                   && (t.at(i + 1).isDigit() || ((t.at(i + 1) == QLatin1Char('+') || t.at(i + 1) == QLatin1Char('-'))
                                                 && i + 2 < n && t.at(i + 2).isDigit()))) {
            i += 2;
        } else {
            break;
        }
    }
    // Suffixes: 10u, 1.5f, 42usize, 3px, 100ms
    while (i < n && (t.at(i).isLetter() || t.at(i) == QLatin1Char('_') || t.at(i) == QLatin1Char('%')))
        ++i;
    return i;
}

// The next non-space character at or after `i`, or a null QChar.
QChar peekNonSpace(const QString &t, int i)
{
    while (i < t.size() && t.at(i).isSpace())
        ++i;
    return i < t.size() ? t.at(i) : QChar();
}

void scanLine(const QString &t, const LangSpec &s, Block &state, QVector<SyntaxSpan> &out)
{
    const int n = t.size();
    out.clear();
    if (n == 0)
        return;

    auto add = [&out](int start, int length, TokenKind kind) {
        if (length > 0)
            out.append({start, length, kind});
    };

    int i = 0;

    // Finish a block that an earlier line opened.
    if (state != NoBlock) {
        QLatin1String end("*/");
        TokenKind kind = TokenKind::Comment;
        switch (state) {
        case CBlock: end = QLatin1String("*/"); break;
        case PyDouble: end = QLatin1String("\"\"\""); kind = TokenKind::String; break;
        case PySingle: end = QLatin1String("'''"); kind = TokenKind::String; break;
        case LuaBlock: end = QLatin1String("]]"); break;
        case XmlComment: end = QLatin1String("-->"); break;
        case NoBlock: break;
        }
        const int idx = t.indexOf(end);
        if (idx < 0) {
            add(0, n, kind);
            return;
        }
        add(0, idx + end.size(), kind);
        i = idx + end.size();
        state = NoBlock;
    }

    int firstNonSpace = 0;
    while (firstNonSpace < n && t.at(firstNonSpace).isSpace())
        ++firstNonSpace;

    if (s.markdown) {
        if (firstNonSpace < n) {
            const QChar c = t.at(firstNonSpace);
            if (c == QLatin1Char('#')) {
                add(firstNonSpace, n - firstNonSpace, TokenKind::Keyword);
                return;
            }
            if (c == QLatin1Char('>')) {
                add(firstNonSpace, n - firstNonSpace, TokenKind::Comment);
                return;
            }
            if (matchAt(t, firstNonSpace, "```")) {
                add(firstNonSpace, n - firstNonSpace, TokenKind::String);
                return;
            }
        }
        while (i < n) {
            if (t.at(i) == QLatin1Char('`')) {
                const int end = endOfQuoted(t, i, QLatin1Char('`'), false);
                add(i, end - i, TokenKind::String);
                i = end;
                continue;
            }
            if (t.at(i) == QLatin1Char('[')) {
                const int close = t.indexOf(QLatin1Char(']'), i);
                if (close > i) {
                    add(i, close - i + 1, TokenKind::Type);
                    i = close + 1;
                    continue;
                }
            }
            ++i;
        }
        return;
    }

    while (i < n) {
        const QChar c = t.at(i);
        if (c.isSpace()) {
            ++i;
            continue;
        }

        // ---- comments
        if (s.luaLongComment && matchAt(t, i, "--[[")) {
            const int close = t.indexOf(QLatin1String("]]"), i + 4);
            if (close < 0) {
                add(i, n - i, TokenKind::Comment);
                state = LuaBlock;
                return;
            }
            add(i, close + 2 - i, TokenKind::Comment);
            i = close + 2;
            continue;
        }
        if (s.lineComment && matchAt(t, i, s.lineComment)) {
            add(i, n - i, TokenKind::Comment);
            return;
        }
        if (s.cBlockComment && c == QLatin1Char('/') && i + 1 < n && t.at(i + 1) == QLatin1Char('*')) {
            const int close = t.indexOf(QLatin1String("*/"), i + 2);
            if (close < 0) {
                add(i, n - i, TokenKind::Comment);
                state = CBlock;
                return;
            }
            add(i, close + 2 - i, TokenKind::Comment);
            i = close + 2;
            continue;
        }
        if (s.xmlComment && matchAt(t, i, "<!--")) {
            const int close = t.indexOf(QLatin1String("-->"), i + 4);
            if (close < 0) {
                add(i, n - i, TokenKind::Comment);
                state = XmlComment;
                return;
            }
            add(i, close + 3 - i, TokenKind::Comment);
            i = close + 3;
            continue;
        }
        if (s.semicolonComment && c == QLatin1Char(';')) {
            add(i, n - i, TokenKind::Comment);
            return;
        }

        // ---- '#': comment, preprocessor, attribute or colour
        if (c == QLatin1Char('#')) {
            if (s.hashAttribute && i + 1 < n
                && (t.at(i + 1) == QLatin1Char('[') || t.at(i + 1) == QLatin1Char('!'))) {
                int j = i + 1, depth = 0;
                while (j < n) {
                    if (t.at(j) == QLatin1Char('['))
                        ++depth;
                    else if (t.at(j) == QLatin1Char(']') && --depth == 0)
                        break;
                    ++j;
                }
                const int end = j < n ? j + 1 : n;
                add(i, end - i, TokenKind::Preprocessor);
                i = end;
                continue;
            }
            if (s.cPreproc && i == firstNonSpace) {
                int j = i + 1;
                while (j < n && t.at(j).isSpace())
                    ++j;
                const int nameStart = j;
                while (j < n && t.at(j).isLetter())
                    ++j;
                add(i, j - i, TokenKind::Preprocessor);
                const QString directive = t.mid(nameStart, j - nameStart);
                i = j;
                if (directive == QLatin1String("include") || directive == QLatin1String("import")) {
                    while (i < n && t.at(i).isSpace())
                        ++i;
                    if (i < n && t.at(i) == QLatin1Char('<')) {
                        const int close = t.indexOf(QLatin1Char('>'), i);
                        const int end = close < 0 ? n : close + 1;
                        add(i, end - i, TokenKind::String);
                        i = end;
                    }
                }
                continue;
            }
            if (s.hashColor && i + 1 < n && isIdent(t.at(i + 1))) {
                int j = i + 1;
                while (j < n && t.at(j).isLetterOrNumber())
                    ++j;
                add(i, j - i, TokenKind::Number);
                i = j;
                continue;
            }
            if (s.hashComment && (i == firstNonSpace || t.at(i - 1).isSpace())) {
                add(i, n - i, TokenKind::Comment);
                return;
            }
            ++i;
            continue;
        }

        // ---- [section] headers
        if (s.sectionHeader && c == QLatin1Char('[') && i == firstNonSpace) {
            const int close = t.lastIndexOf(QLatin1Char(']'));
            const int end = close < i ? n : close + 1;
            add(i, end - i, TokenKind::Preprocessor);
            i = end;
            continue;
        }

        // ---- <tag …>
        if (s.xmlTags && c == QLatin1Char('<')) {
            int j = i + 1;
            if (j < n && (t.at(j) == QLatin1Char('/') || t.at(j) == QLatin1Char('!') || t.at(j) == QLatin1Char('?')))
                ++j;
            const int nameStart = j;
            while (j < n && (isIdent(t.at(j)) || t.at(j) == QLatin1Char('-') || t.at(j) == QLatin1Char(':')))
                ++j;
            if (j > nameStart) {
                add(i, j - i, TokenKind::Keyword);
                i = j;
                continue;
            }
            ++i;
            continue;
        }

        // ---- decorators / at-rules
        if (s.atDecorator && c == QLatin1Char('@') && i + 1 < n && isIdentStart(t.at(i + 1))) {
            int j = i + 1;
            while (j < n && (isIdent(t.at(j)) || t.at(j) == QLatin1Char('.') || t.at(j) == QLatin1Char('-')))
                ++j;
            add(i, j - i, TokenKind::Preprocessor);
            i = j;
            continue;
        }

        // ---- $VAR, ${…}
        if (s.dollarVar && c == QLatin1Char('$') && i + 1 < n) {
            const QChar next = t.at(i + 1);
            if (next == QLatin1Char('{')) {
                const int close = t.indexOf(QLatin1Char('}'), i + 2);
                const int end = close < 0 ? n : close + 1;
                add(i, end - i, TokenKind::Type);
                i = end;
                continue;
            }
            if (isIdent(next)) {
                int j = i + 1;
                while (j < n && isIdent(t.at(j)))
                    ++j;
                add(i, j - i, TokenKind::Type);
                i = j;
                continue;
            }
            ++i;
            continue;
        }

        // ---- strings
        if (s.pyTriple && (matchAt(t, i, "\"\"\"") || matchAt(t, i, "'''"))) {
            const bool dbl = t.at(i) == QLatin1Char('"');
            const QLatin1String quote(dbl ? "\"\"\"" : "'''");
            const int close = t.indexOf(quote, i + 3);
            if (close < 0) {
                add(i, n - i, TokenKind::String);
                state = dbl ? PyDouble : PySingle;
                return;
            }
            add(i, close + 3 - i, TokenKind::String);
            i = close + 3;
            continue;
        }
        if (s.doubleQuote && c == QLatin1Char('"')) {
            const int end = endOfQuoted(t, i, QLatin1Char('"'), true);
            add(i, end - i, TokenKind::String);
            i = end;
            continue;
        }
        if (s.singleQuoteString && c == QLatin1Char('\'')) {
            const int end = endOfQuoted(t, i, QLatin1Char('\''), s.lineComment != nullptr || s.pyTriple);
            add(i, end - i, TokenKind::String);
            i = end;
            continue;
        }
        if (s.backtick && c == QLatin1Char('`')) {
            const int end = endOfQuoted(t, i, QLatin1Char('`'), true);
            add(i, end - i, TokenKind::String);
            i = end;
            continue;
        }
        if (s.charLiteral && c == QLatin1Char('\'')) {
            // 'a', '\n', '\x41' — but not a Rust lifetime ('a), and an
            // unterminated quote must not swallow the rest of the line.
            const int end = endOfQuoted(t, i, QLatin1Char('\''), true);
            const bool closed = end - i >= 2 && end <= n && t.at(end - 1) == QLatin1Char('\'');
            if (closed && end - i <= 12) {
                add(i, end - i, TokenKind::String);
                i = end;
                continue;
            }
            ++i;
            continue;
        }

        // ---- numbers
        if (s.numbers && (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && t.at(i + 1).isDigit()))) {
            // Not a number when it is the tail of an identifier (foo2).
            if (i > 0 && isIdent(t.at(i - 1))) {
                ++i;
                continue;
            }
            const int end = endOfNumber(t, i);
            add(i, end - i, TokenKind::Number);
            i = end;
            continue;
        }

        // ---- identifiers
        if (isIdentStart(c)) {
            int j = i;
            while (j < n && isIdent(t.at(j)))
                ++j;
            const QString word = t.mid(i, j - i);

            // Prefixed string literals: r"", f"", b'', u8"", R"(…)", r#"…"#
            if (j < n && isStringPrefix(word)
                && (t.at(j) == QLatin1Char('"')
                    || ((s.singleQuoteString || s.charLiteral) && t.at(j) == QLatin1Char('\'')))) {
                const int end = endOfQuoted(t, j, t.at(j), true);
                add(i, end - i, TokenKind::String);
                i = end;
                continue;
            }
            if (s.rustRaw && word == QLatin1String("r") && j < n && t.at(j) == QLatin1Char('#')) {
                int hashes = 0;
                while (j + hashes < n && t.at(j + hashes) == QLatin1Char('#'))
                    ++hashes;
                if (j + hashes < n && t.at(j + hashes) == QLatin1Char('"')) {
                    const QString terminator = QLatin1Char('"') + QString(hashes, QLatin1Char('#'));
                    const int close = t.indexOf(terminator, j + hashes + 1);
                    const int end = close < 0 ? n : close + terminator.size();
                    add(i, end - i, TokenKind::String);
                    i = end;
                    continue;
                }
            }

            const QString key = s.foldCase ? word.toLower() : word;
            if (s.keywords && s.keywords->contains(key)) {
                add(i, j - i, TokenKind::Keyword);
            } else if (s.types && s.types->contains(key)) {
                add(i, j - i, TokenKind::Type);
            } else {
                const QChar next = peekNonSpace(t, j);
                if (s.keyBeforeColon && next == QLatin1Char(':'))
                    add(i, j - i, s.colonKind);
                else if (s.keyBeforeEquals && next == QLatin1Char('='))
                    add(i, j - i, TokenKind::Type);
                else if (s.functionCalls && next == QLatin1Char('('))
                    add(i, j - i, TokenKind::Function);
            }
            i = j;
            continue;
        }

        ++i;
    }
}

} // namespace

// ------------------------------------------------------------------ the API

Language SyntaxHighlighter::languageFor(const QString &path, const DiffDocument *docForShebang)
{
    static const QHash<QString, Language> byExtension = {
        {QStringLiteral("c"), Language::Cpp},       {QStringLiteral("h"), Language::Cpp},
        {QStringLiteral("cc"), Language::Cpp},      {QStringLiteral("cpp"), Language::Cpp},
        {QStringLiteral("cxx"), Language::Cpp},     {QStringLiteral("c++"), Language::Cpp},
        {QStringLiteral("hpp"), Language::Cpp},     {QStringLiteral("hh"), Language::Cpp},
        {QStringLiteral("hxx"), Language::Cpp},     {QStringLiteral("inl"), Language::Cpp},
        {QStringLiteral("ipp"), Language::Cpp},     {QStringLiteral("m"), Language::Cpp},
        {QStringLiteral("mm"), Language::Cpp},
        {QStringLiteral("py"), Language::Python},   {QStringLiteral("pyw"), Language::Python},
        {QStringLiteral("pyi"), Language::Python},
        {QStringLiteral("js"), Language::JavaScript}, {QStringLiteral("jsx"), Language::JavaScript},
        {QStringLiteral("mjs"), Language::JavaScript}, {QStringLiteral("cjs"), Language::JavaScript},
        {QStringLiteral("ts"), Language::TypeScript}, {QStringLiteral("tsx"), Language::TypeScript},
        {QStringLiteral("mts"), Language::TypeScript}, {QStringLiteral("cts"), Language::TypeScript},
        {QStringLiteral("rs"), Language::Rust},     {QStringLiteral("go"), Language::Go},
        {QStringLiteral("sh"), Language::Shell},    {QStringLiteral("bash"), Language::Shell},
        {QStringLiteral("zsh"), Language::Shell},   {QStringLiteral("ksh"), Language::Shell},
        {QStringLiteral("json"), Language::Json},   {QStringLiteral("jsonc"), Language::Json},
        {QStringLiteral("yml"), Language::Yaml},    {QStringLiteral("yaml"), Language::Yaml},
        {QStringLiteral("toml"), Language::Toml},
        {QStringLiteral("md"), Language::Markdown}, {QStringLiteral("markdown"), Language::Markdown},
        {QStringLiteral("css"), Language::Css},     {QStringLiteral("scss"), Language::Css},
        {QStringLiteral("sass"), Language::Css},    {QStringLiteral("less"), Language::Css},
        {QStringLiteral("html"), Language::Html},   {QStringLiteral("htm"), Language::Html},
        {QStringLiteral("xhtml"), Language::Html},  {QStringLiteral("xml"), Language::Html},
        {QStringLiteral("svg"), Language::Html},    {QStringLiteral("qrc"), Language::Html},
        {QStringLiteral("ui"), Language::Html},
        {QStringLiteral("java"), Language::Java},   {QStringLiteral("kt"), Language::Kotlin},
        {QStringLiteral("kts"), Language::Kotlin},  {QStringLiteral("cs"), Language::CSharp},
        {QStringLiteral("dart"), Language::Dart},
        {QStringLiteral("rb"), Language::Ruby},     {QStringLiteral("rake"), Language::Ruby},
        {QStringLiteral("gemspec"), Language::Ruby},
        {QStringLiteral("lua"), Language::Lua},
        {QStringLiteral("pro"), Language::QMake},   {QStringLiteral("pri"), Language::QMake},
        {QStringLiteral("prf"), Language::QMake},
        {QStringLiteral("cmake"), Language::CMake},
        {QStringLiteral("mk"), Language::Make},     {QStringLiteral("make"), Language::Make},
        {QStringLiteral("ini"), Language::Ini},     {QStringLiteral("conf"), Language::Ini},
        {QStringLiteral("cfg"), Language::Ini},     {QStringLiteral("desktop"), Language::Ini},
        {QStringLiteral("service"), Language::Ini}, {QStringLiteral("properties"), Language::Ini},
        {QStringLiteral("sql"), Language::Sql},
    };
    static const QHash<QString, Language> byName = {
        {QStringLiteral("makefile"), Language::Make},
        {QStringLiteral("gnumakefile"), Language::Make},
        {QStringLiteral("cmakelists.txt"), Language::CMake},
        {QStringLiteral("dockerfile"), Language::Dockerfile},
        {QStringLiteral("containerfile"), Language::Dockerfile},
        {QStringLiteral(".bashrc"), Language::Shell},
        {QStringLiteral(".bash_profile"), Language::Shell},
        {QStringLiteral(".bash_aliases"), Language::Shell},
        {QStringLiteral(".bash_logout"), Language::Shell},
        {QStringLiteral(".profile"), Language::Shell},
        {QStringLiteral(".zshrc"), Language::Shell},
        {QStringLiteral(".zshenv"), Language::Shell},
        {QStringLiteral(".zprofile"), Language::Shell},
        {QStringLiteral(".xinitrc"), Language::Shell},
        {QStringLiteral(".gitconfig"), Language::Ini},
        {QStringLiteral(".editorconfig"), Language::Ini},
        {QStringLiteral("pkgbuild"), Language::Shell},
    };

    const QFileInfo info(path);
    const QString name = info.fileName().toLower();
    if (byName.contains(name))
        return byName.value(name);
    if (name.startsWith(QLatin1String("dockerfile")))
        return Language::Dockerfile;
    if (name.startsWith(QLatin1String("makefile")))
        return Language::Make;
    const Language byExt = byExtension.value(info.suffix().toLower(), Language::None);
    if (byExt != Language::None)
        return byExt;

    // Nothing in the name: try the interpreter line of the first content line.
    if (docForShebang) {
        for (const DiffLine &l : docForShebang->lines) {
            if (l.state == DiffLine::Header || l.state == DiffLine::Empty)
                continue;
            const QString first = l.text.trimmed();
            if (!first.startsWith(QLatin1String("#!")))
                break; // only the very first content line can carry a shebang
            if (first.contains(QLatin1String("python")))
                return Language::Python;
            if (first.contains(QLatin1String("node")))
                return Language::JavaScript;
            if (first.contains(QLatin1String("ruby")))
                return Language::Ruby;
            if (first.contains(QLatin1String("lua")))
                return Language::Lua;
            if (first.contains(QLatin1String("bash")) || first.contains(QLatin1String("zsh"))
                || first.contains(QLatin1String("/sh")) || first.endsWith(QLatin1String(" sh")))
                return Language::Shell;
            break;
        }
    }
    return Language::None;
}

QString SyntaxHighlighter::displayName(Language lang)
{
    switch (lang) {
    case Language::None: return QStringLiteral("Plain text");
    case Language::Cpp: return QStringLiteral("C/C++");
    case Language::CSharp: return QStringLiteral("C#");
    case Language::Java: return QStringLiteral("Java");
    case Language::Kotlin: return QStringLiteral("Kotlin");
    case Language::Dart: return QStringLiteral("Dart");
    case Language::JavaScript: return QStringLiteral("JavaScript");
    case Language::TypeScript: return QStringLiteral("TypeScript");
    case Language::Python: return QStringLiteral("Python");
    case Language::Rust: return QStringLiteral("Rust");
    case Language::Go: return QStringLiteral("Go");
    case Language::Ruby: return QStringLiteral("Ruby");
    case Language::Lua: return QStringLiteral("Lua");
    case Language::Shell: return QStringLiteral("Shell");
    case Language::Sql: return QStringLiteral("SQL");
    case Language::Json: return QStringLiteral("JSON");
    case Language::Yaml: return QStringLiteral("YAML");
    case Language::Toml: return QStringLiteral("TOML");
    case Language::Ini: return QStringLiteral("INI");
    case Language::Markdown: return QStringLiteral("Markdown");
    case Language::Css: return QStringLiteral("CSS");
    case Language::Html: return QStringLiteral("HTML/XML");
    case Language::Make: return QStringLiteral("Makefile");
    case Language::QMake: return QStringLiteral("qmake");
    case Language::CMake: return QStringLiteral("CMake");
    case Language::Dockerfile: return QStringLiteral("Dockerfile");
    }
    return QString();
}

void SyntaxHighlighter::clear(DiffDocument &doc)
{
    for (DiffLine &l : doc.lines)
        l.syntax.clear();
}

void SyntaxHighlighter::highlight(DiffDocument &doc, Language lang)
{
    if (lang == Language::None) {
        clear(doc);
        return;
    }
    const LangSpec &spec = specFor(lang);

    // The diff interleaves two files. `oldState` follows the base version
    // (context + removed lines), `newState` the working tree (context +
    // added lines), so an unterminated comment on one side cannot colour the
    // other side's lines.
    Block oldState = NoBlock, newState = NoBlock;
    QVector<SyntaxSpan> scratch;

    for (DiffLine &l : doc.lines) {
        switch (l.state) {
        case DiffLine::Header:
        case DiffLine::Empty:
            l.syntax.clear();
            break;
        case DiffLine::Added:
            scanLine(l.text, spec, newState, l.syntax);
            break;
        case DiffLine::Removed:
            scanLine(l.text, spec, oldState, l.syntax);
            break;
        case DiffLine::Normal:
            if (oldState == newState) {
                scanLine(l.text, spec, newState, l.syntax);
                oldState = newState;
            } else {
                // The two versions disagree here (a change edited a comment
                // opener). Show the working tree's reading and keep the old
                // side's state moving so it can resynchronise.
                scanLine(l.text, spec, oldState, scratch);
                scanLine(l.text, spec, newState, l.syntax);
            }
            break;
        }
    }
}
