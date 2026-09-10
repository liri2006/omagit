#include "SyntaxHighlighter.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <array>

namespace {

// ---------------------------------------------------------------- word lists

// Space separated; the sets are built once, with the language table.
constexpr const char *kCppKeywords =
    "alignas alignof and and_eq asm bitand bitor break case catch class compl concept const "
    "consteval constexpr constinit const_cast continue co_await co_return co_yield decltype "
    "default delete do dynamic_cast else enum explicit export extern false final for friend goto "
    "if inline mutable namespace new noexcept not not_eq nullptr operator or or_eq override "
    "private protected public register reinterpret_cast requires return sizeof static "
    "static_assert static_cast struct switch template this thread_local throw true try typedef "
    "typeid typename union using virtual volatile while xor xor_eq";
constexpr const char *kCppTypes =
    "auto bool char char8_t char16_t char32_t double float int long short signed unsigned void "
    "wchar_t size_t ssize_t ptrdiff_t intptr_t uintptr_t int8_t int16_t int32_t int64_t uint8_t "
    "uint16_t uint32_t uint64_t std string wstring string_view vector map unordered_map set "
    "unordered_set pair tuple array deque list optional variant function shared_ptr unique_ptr "
    "weak_ptr";

constexpr const char *kPythonKeywords =
    "and as assert async await break class continue def del elif else except finally for from "
    "global if import in is lambda match nonlocal not or pass raise return try while with yield";
constexpr const char *kPythonTypes =
    "True False None self cls bool bytes bytearray complex dict float frozenset int list object "
    "set str tuple type abs all any enumerate isinstance issubclass len max min open print range "
    "repr reversed round sorted sum super zip Exception ValueError TypeError KeyError IndexError "
    "RuntimeError NotImplementedError";

constexpr const char *kJsKeywords =
    "abstract as async await break case catch class const constructor continue debugger declare "
    "default delete do else enum export extends finally for from function get if implements "
    "import in infer instanceof interface is keyof let namespace new of package private "
    "protected public readonly require return satisfies set static super switch throw try type "
    "typeof var void while with yield";
constexpr const char *kJsTypes =
    "any bigint boolean never null number object string symbol this undefined unknown true false "
    "NaN Infinity Array Boolean Date Error JSON Map Math Number Object Promise RegExp Set String "
    "Symbol WeakMap WeakSet console document globalThis process window";

constexpr const char *kRustKeywords =
    "as async await break const continue crate dyn else enum extern false fn for if impl in let "
    "loop match mod move mut pub ref return self Self static struct super trait true type unsafe "
    "use where while";
constexpr const char *kRustTypes =
    "bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize String Vec Option "
    "Some None Result Ok Err Box Rc Arc RefCell Cell Cow HashMap HashSet BTreeMap BTreeSet";

constexpr const char *kGoKeywords =
    "break case chan const continue default defer else fallthrough for func go goto if import "
    "interface map package range return select struct switch type var";
constexpr const char *kGoTypes =
    "any bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune "
    "string uint uint8 uint16 uint32 uint64 uintptr true false nil iota append cap close copy "
    "delete len make new panic print println recover";

constexpr const char *kShellKeywords =
    "if then else elif fi case esac for while until do done in function select time coproc "
    "return break continue exit export local readonly declare typeset unset shift source alias "
    "eval exec set trap";
constexpr const char *kShellTypes =
    "echo printf cd pwd read test true false let mapfile getopts command builtin type hash umask "
    "wait kill jobs bg fg pushd popd dirs";

constexpr const char *kJavaKeywords =
    "abstract assert break case catch class const continue default do else enum extends final "
    "finally for goto if implements import instanceof interface native new package permits "
    "private protected public record return sealed static strictfp super switch synchronized "
    "this throw throws transient try var volatile while yield";
constexpr const char *kJavaTypes =
    "boolean byte char double float int long short void true false null String Object Integer "
    "Double Float Long Short Boolean Character List Map Set Collection ArrayList HashMap HashSet "
    "Optional Stream System";

constexpr const char *kKotlinKeywords =
    "as break by catch class companion const constructor continue crossinline data do dynamic "
    "else enum expect external final finally for fun get if import in infix init inline inner "
    "interface internal is lateinit noinline object open operator out override package private "
    "protected public reified return sealed set super suspend tailrec this throw try typealias "
    "val var vararg when where while";
constexpr const char *kKotlinTypes =
    "Any Boolean Byte Char Double Float Int Long Nothing Short String Unit Array List Map Set "
    "MutableList MutableMap MutableSet Pair Triple true false null it";

constexpr const char *kDartKeywords =
    "abstract as assert async await base break case catch class const continue covariant default "
    "deferred do dynamic else enum export extends extension external factory false final finally "
    "for get hide if implements import in interface is late library mixin new null on operator "
    "part required rethrow return sealed set show static super switch sync this throw true try "
    "typedef var void when while with yield";
constexpr const char *kDartTypes =
    "int double num bool String List Map Set Iterable Future Stream Object Null Never Function "
    "Symbol Type Duration DateTime Uri BigInt Runes Record Widget BuildContext State StatelessWidget "
    "StatefulWidget print identical";

constexpr const char *kCsharpKeywords =
    "abstract as async await base break case catch checked class const continue default delegate "
    "do else enum event explicit extern finally fixed for foreach get goto if implicit in "
    "interface internal is lock namespace new operator out override params partial private "
    "protected public readonly record ref return sealed set sizeof stackalloc static struct "
    "switch this throw try typeof unchecked unsafe using value virtual volatile while yield";
constexpr const char *kCsharpTypes =
    "bool byte char decimal double dynamic float int long nint nuint object sbyte short string "
    "uint ulong ushort var void true false null List Dictionary Task String Int32 Console "
    "Exception IEnumerable";

constexpr const char *kRubyKeywords =
    "BEGIN END alias and begin break case class def defined? do else elsif end ensure false for "
    "if in module next nil not or redo rescue retry return self super then true undef unless "
    "until when while yield require require_relative include extend attr_accessor attr_reader "
    "attr_writer";
constexpr const char *kRubyTypes =
    "Array Class Comparable Enumerable Exception Float Hash Integer Module Numeric Proc Range "
    "Regexp String Struct Symbol Time puts print p lambda new";

constexpr const char *kLuaKeywords =
    "and break do else elseif end false for function goto if in local nil not or repeat return "
    "then true until while";
constexpr const char *kLuaTypes =
    "assert collectgarbage coroutine dofile error getmetatable io ipairs load loadstring math "
    "next os pairs pcall print rawequal rawget rawlen rawset require select self setmetatable "
    "string table tonumber tostring type unpack xpcall";

constexpr const char *kSqlKeywords =
    "add all alter and as asc begin between by cascade case check column commit constraint "
    "create cross default delete desc distinct drop else end exists foreign from full group "
    "having if in index inner insert into is join key left like limit not null offset on or "
    "order outer primary references replace returning right rollback select set table then "
    "transaction union unique update using values view when where with";
constexpr const char *kSqlTypes =
    "bigint blob boolean bytea char date datetime decimal double float int integer json jsonb "
    "money numeric precision real serial smallint text time timestamp uuid varchar";

constexpr const char *kJsonKeywords = "true false null";
constexpr const char *kYamlKeywords = "true false null yes no on off True False Null Yes No On Off ~";
// Shared by TOML and INI, which both only know the two boolean literals.
constexpr const char *kBooleanKeywords = "true false";

constexpr const char *kCmakeKeywords =
    "add_compile_definitions add_custom_command add_custom_target add_definitions add_dependencies "
    "add_executable add_library add_subdirectory add_test break cmake_minimum_required configure_file "
    "continue else elseif endforeach endfunction endif endmacro endwhile execute_process find_library "
    "find_package find_path find_program foreach function get_filename_component get_target_property "
    "if include install list macro math message option project return set set_target_properties "
    "string target_compile_definitions target_compile_options target_include_directories "
    "target_link_libraries target_sources unset while";
constexpr const char *kCmakeTypes =
    "AND OR NOT EQUAL STREQUAL MATCHES EXISTS DEFINED PRIVATE PUBLIC INTERFACE REQUIRED QUIET "
    "COMPONENTS TARGET DESTINATION CACHE FORCE STATIC SHARED MODULE ON OFF TRUE FALSE";

constexpr const char *kMakeKeywords =
    "define else endef endif export ifdef ifeq ifndef ifneq include override sinclude unexport "
    "vpath -include";
constexpr const char *kMakeTypes = "MAKE MAKEFLAGS CC CXX CFLAGS CXXFLAGS LDFLAGS SHELL PHONY";

constexpr const char *kQmakeKeywords =
    "TEMPLATE TARGET CONFIG QT SOURCES HEADERS FORMS RESOURCES DEFINES INCLUDEPATH LIBS DEPENDPATH "
    "DESTDIR OBJECTS_DIR MOC_DIR UI_DIR RCC_DIR VERSION SUBDIRS PKGCONFIG DISTFILES TRANSLATIONS "
    "QMAKE_CXXFLAGS QMAKE_CFLAGS QMAKE_LFLAGS QMAKE_TARGET INSTALLS";
constexpr const char *kQmakeTypes =
    "android contains count defineReplace defineTest else equals error eval exists for greaterThan "
    "include isEmpty lessThan macx message requires return unix warning win32";

constexpr const char *kDockerKeywords =
    "add arg cmd copy entrypoint env expose from healthcheck label maintainer onbuild run shell "
    "stopsignal user volume workdir as";

QStringList splitWords(const char *words)
{
    return words ? QString::fromLatin1(words).split(QLatin1Char(' '), Qt::SkipEmptyParts) : QStringList();
}

QSet<QString> setOf(const char *words)
{
    QSet<QString> out;
    const QStringList list = splitWords(words);
    for (const QString &w : list)
        out.insert(w);
    return out;
}

// --------------------------------------------------------------- language spec

// Multi-line constructs the scanner may be sitting inside when a line starts.
enum Block {
    NoBlock,
    CBlock,      // /* ... */
    PyDouble,    // """ ... """
    PySingle,    // ''' ... '''
    LuaBlock,    // --[[ ... ]]
    XmlBlock,    // <!-- ... -->
};

// What the scanner does for a language. Anything not listed for a language is
// off; the two "No…" flags switch off something that is on nearly everywhere.
enum Flag : quint32 {
    FoldCase           = 1u << 0,  // keyword lookup is case-insensitive (SQL, CMake, Dockerfile)
    HashComment        = 1u << 1,  // '#' to end of line
    SemicolonComment   = 1u << 2,  // ';' to end of line (ini)
    CBlockComment      = 1u << 3,  // /* … */
    LuaLongComment     = 1u << 4,  // --[[ … ]]
    XmlComment         = 1u << 5,  // <!-- … -->
    NoDoubleQuote      = 1u << 6,  // "…" is not a literal (markdown)
    SingleQuote        = 1u << 7,  // '…' is a string
    SingleQuoteEscapes = 1u << 8,  // … and a backslash escapes inside it
    CharLiteral        = 1u << 9,  // '…' is a char literal (and 'a may be a lifetime)
    Backtick           = 1u << 10, // `…` template / command string
    PyTriple           = 1u << 11, // """…""" and '''…'''
    RustRaw            = 1u << 12, // r#"…"#
    NoNumbers          = 1u << 13, // digits are not literals (markdown, html)
    CPreproc           = 1u << 14, // #include / #define at the start of a line
    AtDecorator        = 1u << 15, // @foo → Preprocessor
    HashAttribute      = 1u << 16, // Rust #[…]
    DollarVar          = 1u << 17, // $VAR / ${…} → Type
    HashColor          = 1u << 18, // CSS #1a2b3c → Number
    SectionHeader      = 1u << 19, // [section] at the start of a line → Preprocessor
    XmlTags            = 1u << 20, // <tag …> → Keyword
    MarkdownText       = 1u << 21, // headings / quotes / `code`
    FunctionCalls      = 1u << 22, // name( → Function
    KeyBeforeColon     = 1u << 23, // name: → Type
    KeyBeforeEquals    = 1u << 24, // name= → Type
    ColonIsTarget      = 1u << 25, // … but in a makefile name: is a rule, so Function
};

// r"", b'', f"", u8"", R"(…)", L"": a short prefix glued to a quote. Only the
// languages that actually have them go looking.
constexpr const char *kStringPrefixes = "rbufxRBUFXL8";
constexpr const char *kDartStringPrefixes = "rR";       // raw strings
constexpr const char *kSqlStringPrefixes = "bBxXnNeE";  // bit, hex, national, escape literals

struct LangSpec {
    QSet<QString> keywords;
    QSet<QString> types;
    const char *lineComment = nullptr;    // "//", "--" …
    const char *stringPrefixes = nullptr;
    quint32 flags = 0;

    bool has(quint32 flag) const { return (flags & flag) != 0; }
};

// The one table: word lists, scanner flags and the file names that pick each
// language. Adding a language means adding an enumerator and one row here.
struct LangRow {
    Language lang;
    const char *keywords;
    const char *types;
    const char *extensions;  // space separated, lower case, no leading dot
    const char *fileNames;   // space separated basenames, lower case
    const char *lineComment;
    const char *stringPrefixes;
    quint32 flags;
};

const LangRow kLanguages[] = {
    {Language::Cpp, kCppKeywords, kCppTypes,
     "c h cc cpp cxx c++ hpp hh hxx inl ipp m mm", nullptr, "//", kStringPrefixes,
     CBlockComment | CharLiteral | CPreproc | FunctionCalls},
    {Language::CSharp, kCsharpKeywords, kCsharpTypes,
     "cs", nullptr, "//", kStringPrefixes,
     CBlockComment | CharLiteral | CPreproc | AtDecorator | FunctionCalls},
    {Language::Java, kJavaKeywords, kJavaTypes,
     "java", nullptr, "//", nullptr,
     CBlockComment | CharLiteral | AtDecorator | FunctionCalls},
    {Language::Kotlin, kKotlinKeywords, kKotlinTypes,
     "kt kts", nullptr, "//", nullptr,
     CBlockComment | CharLiteral | AtDecorator | FunctionCalls},
    {Language::Dart, kDartKeywords, kDartTypes,
     "dart", nullptr, "//", kDartStringPrefixes,
     CBlockComment | SingleQuote | SingleQuoteEscapes | PyTriple | AtDecorator | DollarVar
         | FunctionCalls},
    {Language::JavaScript, kJsKeywords, kJsTypes,
     "js jsx mjs cjs", nullptr, "//", nullptr,
     CBlockComment | SingleQuote | SingleQuoteEscapes | Backtick | AtDecorator | FunctionCalls},
    {Language::TypeScript, kJsKeywords, kJsTypes,
     "ts tsx mts cts", nullptr, "//", nullptr,
     CBlockComment | SingleQuote | SingleQuoteEscapes | Backtick | AtDecorator | FunctionCalls},
    {Language::Python, kPythonKeywords, kPythonTypes,
     "py pyw pyi", nullptr, nullptr, kStringPrefixes,
     HashComment | SingleQuote | SingleQuoteEscapes | PyTriple | AtDecorator | FunctionCalls},
    {Language::Rust, kRustKeywords, kRustTypes,
     "rs", nullptr, "//", kStringPrefixes,
     CBlockComment | CharLiteral | RustRaw | HashAttribute | FunctionCalls},
    {Language::Go, kGoKeywords, kGoTypes,
     "go", nullptr, "//", nullptr,
     CBlockComment | CharLiteral | Backtick | FunctionCalls},
    {Language::Ruby, kRubyKeywords, kRubyTypes,
     "rb rake gemspec", nullptr, nullptr, nullptr,
     HashComment | SingleQuote | DollarVar | FunctionCalls},
    {Language::Lua, kLuaKeywords, kLuaTypes,
     "lua", nullptr, "--", nullptr,
     LuaLongComment | SingleQuote | SingleQuoteEscapes | FunctionCalls},
    {Language::Shell, kShellKeywords, kShellTypes,
     "sh bash zsh ksh",
     ".bashrc .bash_profile .bash_aliases .bash_logout .profile .zshrc .zshenv .zprofile "
     ".xinitrc pkgbuild",
     nullptr, nullptr,
     HashComment | SingleQuote | Backtick | DollarVar},
    {Language::Sql, kSqlKeywords, kSqlTypes,
     "sql", nullptr, "--", kSqlStringPrefixes,
     FoldCase | CBlockComment | SingleQuote | SingleQuoteEscapes | FunctionCalls},
    // "//" is for .jsonc and is harmless in strict JSON.
    {Language::Json, kJsonKeywords, nullptr,
     "json jsonc", nullptr, "//", nullptr,
     0},
    {Language::Yaml, kYamlKeywords, nullptr,
     "yml yaml", nullptr, nullptr, nullptr,
     HashComment | SingleQuote | KeyBeforeColon},
    {Language::Toml, kBooleanKeywords, nullptr,
     "toml", nullptr, nullptr, nullptr,
     HashComment | SingleQuote | SectionHeader | KeyBeforeEquals},
    {Language::Ini, kBooleanKeywords, nullptr,
     "ini conf cfg desktop service properties", ".gitconfig .editorconfig", nullptr, nullptr,
     HashComment | SemicolonComment | SectionHeader | KeyBeforeEquals},
    {Language::Markdown, nullptr, nullptr,
     "md markdown", nullptr, nullptr, nullptr,
     MarkdownText | NoNumbers | NoDoubleQuote},
    {Language::Css, nullptr, nullptr,
     "css scss sass less", nullptr, nullptr, nullptr,
     CBlockComment | SingleQuote | AtDecorator | HashColor | KeyBeforeColon},
    {Language::Html, nullptr, nullptr,
     "html htm xhtml xml svg qrc ui", nullptr, nullptr, nullptr,
     XmlComment | XmlTags | SingleQuote | KeyBeforeEquals | NoNumbers},
    {Language::Make, kMakeKeywords, kMakeTypes,
     "mk make", "makefile gnumakefile", nullptr, nullptr,
     HashComment | SingleQuote | DollarVar | KeyBeforeColon | ColonIsTarget},
    {Language::QMake, kQmakeKeywords, kQmakeTypes,
     "pro pri prf", nullptr, nullptr, nullptr,
     HashComment | SingleQuote | DollarVar},
    {Language::CMake, kCmakeKeywords, kCmakeTypes,
     "cmake", "cmakelists.txt", nullptr, nullptr,
     FoldCase | HashComment | DollarVar | FunctionCalls},
    {Language::Dockerfile, kDockerKeywords, nullptr,
     nullptr, "dockerfile containerfile", nullptr, nullptr,
     FoldCase | HashComment | SingleQuote | DollarVar},
};

// Dockerfile is the last enumerator, so this covers every language.
constexpr size_t kLanguageCount = size_t(Language::Dockerfile) + 1;

struct LangTable {
    std::array<LangSpec, kLanguageCount> specs; // Language::None keeps the defaults
    QHash<QString, Language> byExtension;
    QHash<QString, Language> byName;
};

// Built once, on the first diff that needs it, and never again.
const LangTable &languageTable()
{
    static const LangTable table = [] {
        LangTable t;
        for (const LangRow &row : kLanguages) {
            LangSpec &spec = t.specs[size_t(row.lang)];
            spec.keywords = setOf(row.keywords);
            spec.types = setOf(row.types);
            spec.lineComment = row.lineComment;
            spec.stringPrefixes = row.stringPrefixes;
            spec.flags = row.flags;
            const QStringList extensions = splitWords(row.extensions);
            for (const QString &extension : extensions)
                t.byExtension.insert(extension, row.lang);
            const QStringList names = splitWords(row.fileNames);
            for (const QString &name : names)
                t.byName.insert(name, row.lang);
        }
        return t;
    }();
    return table;
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

inline bool isStringPrefix(const QString &word, const char *allowed)
{
    if (!allowed || word.isEmpty() || word.size() > 3)
        return false;
    for (const QChar c : word) {
        if (!QLatin1String(allowed).contains(c))
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

// One line of one language. Each step looks at the character at `i` and returns
// how many characters it consumed, or 0 when it does not apply; run() calls
// them in a fixed order, and that order is the tokeniser's precedence.
struct LineScanner {
    LineScanner(const QString &text, const LangSpec &spec, Block &blockState, QVector<SyntaxSpan> &spans)
        : t(text), n(text.size()), s(spec), state(blockState), out(spans) {}

    void run();

    void add(int start, int length, TokenKind kind);
    int resumeBlock();
    void scanMarkdown(int i);
    int comment(int i);
    int hash(int i);
    int sectionHeader(int i);
    int xmlTag(int i);
    int decorator(int i);
    int dollarVar(int i);
    int quoted(int i);
    int number(int i);
    int identifier(int i);

    const QString &t;
    const int n;
    const LangSpec &s;
    Block &state;
    QVector<SyntaxSpan> &out;
    int firstNonSpace = 0;
};

void LineScanner::add(int start, int length, TokenKind kind)
{
    if (length > 0)
        out.append({start, length, kind});
}

// Finishes a block that an earlier line opened. Returns where the rest of the
// line starts, which is the line length when the block still has not closed.
int LineScanner::resumeBlock()
{
    if (state == NoBlock)
        return 0;
    QLatin1String end("*/");
    TokenKind kind = TokenKind::Comment;
    switch (state) {
    case CBlock: end = QLatin1String("*/"); break;
    case PyDouble: end = QLatin1String("\"\"\""); kind = TokenKind::String; break;
    case PySingle: end = QLatin1String("'''"); kind = TokenKind::String; break;
    case LuaBlock: end = QLatin1String("]]"); break;
    case XmlBlock: end = QLatin1String("-->"); break;
    case NoBlock: break;
    }
    const int idx = t.indexOf(end);
    if (idx < 0) {
        add(0, n, kind);
        return n;
    }
    add(0, idx + end.size(), kind);
    state = NoBlock;
    return idx + end.size();
}

// Markdown has no tokens to speak of: a heading, a quote or a fence claims the
// whole line, otherwise only `code` and [links] are marked up.
void LineScanner::scanMarkdown(int i)
{
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
}

int LineScanner::comment(int i)
{
    const QChar c = t.at(i);
    if (s.has(LuaLongComment) && matchAt(t, i, "--[[")) {
        const int close = t.indexOf(QLatin1String("]]"), i + 4);
        if (close < 0) {
            add(i, n - i, TokenKind::Comment);
            state = LuaBlock;
            return n - i;
        }
        add(i, close + 2 - i, TokenKind::Comment);
        return close + 2 - i;
    }
    if (s.lineComment && matchAt(t, i, s.lineComment)) {
        add(i, n - i, TokenKind::Comment);
        return n - i;
    }
    if (s.has(CBlockComment) && c == QLatin1Char('/') && i + 1 < n && t.at(i + 1) == QLatin1Char('*')) {
        const int close = t.indexOf(QLatin1String("*/"), i + 2);
        if (close < 0) {
            add(i, n - i, TokenKind::Comment);
            state = CBlock;
            return n - i;
        }
        add(i, close + 2 - i, TokenKind::Comment);
        return close + 2 - i;
    }
    if (s.has(XmlComment) && matchAt(t, i, "<!--")) {
        const int close = t.indexOf(QLatin1String("-->"), i + 4);
        if (close < 0) {
            add(i, n - i, TokenKind::Comment);
            state = XmlBlock;
            return n - i;
        }
        add(i, close + 3 - i, TokenKind::Comment);
        return close + 3 - i;
    }
    if (s.has(SemicolonComment) && c == QLatin1Char(';')) {
        add(i, n - i, TokenKind::Comment);
        return n - i;
    }
    return 0;
}

// '#' is a comment, a preprocessor directive, an attribute or a colour,
// depending on the language and on where it sits.
int LineScanner::hash(int i)
{
    if (t.at(i) != QLatin1Char('#'))
        return 0;
    if (s.has(HashAttribute) && i + 1 < n
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
        return end - i;
    }
    if (s.has(CPreproc) && i == firstNonSpace) {
        int j = i + 1;
        while (j < n && t.at(j).isSpace())
            ++j;
        const int nameStart = j;
        while (j < n && t.at(j).isLetter())
            ++j;
        add(i, j - i, TokenKind::Preprocessor);
        const QString directive = t.mid(nameStart, j - nameStart);
        int end = j;
        if (directive == QLatin1String("include") || directive == QLatin1String("import")) {
            while (end < n && t.at(end).isSpace())
                ++end;
            if (end < n && t.at(end) == QLatin1Char('<')) {
                const int close = t.indexOf(QLatin1Char('>'), end);
                const int stop = close < 0 ? n : close + 1;
                add(end, stop - end, TokenKind::String);
                end = stop;
            }
        }
        return end - i;
    }
    if (s.has(HashColor) && i + 1 < n && isIdent(t.at(i + 1))) {
        int j = i + 1;
        while (j < n && t.at(j).isLetterOrNumber())
            ++j;
        add(i, j - i, TokenKind::Number);
        return j - i;
    }
    if (s.has(HashComment) && (i == firstNonSpace || t.at(i - 1).isSpace())) {
        add(i, n - i, TokenKind::Comment);
        return n - i;
    }
    return 1;
}

int LineScanner::sectionHeader(int i)
{
    if (!s.has(SectionHeader) || t.at(i) != QLatin1Char('[') || i != firstNonSpace)
        return 0;
    const int close = t.lastIndexOf(QLatin1Char(']'));
    const int end = close < i ? n : close + 1;
    add(i, end - i, TokenKind::Preprocessor);
    return end - i;
}

int LineScanner::xmlTag(int i)
{
    if (!s.has(XmlTags) || t.at(i) != QLatin1Char('<'))
        return 0;
    int j = i + 1;
    if (j < n && (t.at(j) == QLatin1Char('/') || t.at(j) == QLatin1Char('!') || t.at(j) == QLatin1Char('?')))
        ++j;
    const int nameStart = j;
    while (j < n && (isIdent(t.at(j)) || t.at(j) == QLatin1Char('-') || t.at(j) == QLatin1Char(':')))
        ++j;
    if (j == nameStart)
        return 1;
    add(i, j - i, TokenKind::Keyword);
    return j - i;
}

int LineScanner::decorator(int i)
{
    if (!s.has(AtDecorator) || t.at(i) != QLatin1Char('@') || i + 1 >= n || !isIdentStart(t.at(i + 1)))
        return 0;
    int j = i + 1;
    while (j < n && (isIdent(t.at(j)) || t.at(j) == QLatin1Char('.') || t.at(j) == QLatin1Char('-')))
        ++j;
    add(i, j - i, TokenKind::Preprocessor);
    return j - i;
}

int LineScanner::dollarVar(int i)
{
    if (!s.has(DollarVar) || t.at(i) != QLatin1Char('$') || i + 1 >= n)
        return 0;
    const QChar next = t.at(i + 1);
    if (next == QLatin1Char('{')) {
        const int close = t.indexOf(QLatin1Char('}'), i + 2);
        const int end = close < 0 ? n : close + 1;
        add(i, end - i, TokenKind::Type);
        return end - i;
    }
    if (isIdent(next)) {
        int j = i + 1;
        while (j < n && isIdent(t.at(j)))
            ++j;
        add(i, j - i, TokenKind::Type);
        return j - i;
    }
    return 1;
}

int LineScanner::quoted(int i)
{
    const QChar c = t.at(i);
    if (s.has(PyTriple) && (matchAt(t, i, "\"\"\"") || matchAt(t, i, "'''"))) {
        const bool dbl = c == QLatin1Char('"');
        const QLatin1String quote(dbl ? "\"\"\"" : "'''");
        const int close = t.indexOf(quote, i + 3);
        if (close < 0) {
            add(i, n - i, TokenKind::String);
            state = dbl ? PyDouble : PySingle;
            return n - i;
        }
        add(i, close + 3 - i, TokenKind::String);
        return close + 3 - i;
    }
    if (!s.has(NoDoubleQuote) && c == QLatin1Char('"')) {
        const int end = endOfQuoted(t, i, QLatin1Char('"'), true);
        add(i, end - i, TokenKind::String);
        return end - i;
    }
    if (s.has(SingleQuote) && c == QLatin1Char('\'')) {
        const int end = endOfQuoted(t, i, QLatin1Char('\''), s.has(SingleQuoteEscapes));
        add(i, end - i, TokenKind::String);
        return end - i;
    }
    if (s.has(Backtick) && c == QLatin1Char('`')) {
        const int end = endOfQuoted(t, i, QLatin1Char('`'), true);
        add(i, end - i, TokenKind::String);
        return end - i;
    }
    if (s.has(CharLiteral) && c == QLatin1Char('\'')) {
        // 'a', '\n', '\x41' — but not a Rust lifetime ('a), and an
        // unterminated quote must not swallow the rest of the line.
        const int end = endOfQuoted(t, i, QLatin1Char('\''), true);
        const bool closed = end - i >= 2 && end <= n && t.at(end - 1) == QLatin1Char('\'');
        if (!closed || end - i > 12)
            return 1;
        add(i, end - i, TokenKind::String);
        return end - i;
    }
    return 0;
}

int LineScanner::number(int i)
{
    const QChar c = t.at(i);
    if (s.has(NoNumbers) || !(c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && t.at(i + 1).isDigit())))
        return 0;
    // Not a number when it is the tail of an identifier (foo2).
    if (i > 0 && isIdent(t.at(i - 1)))
        return 1;
    const int end = endOfNumber(t, i);
    add(i, end - i, TokenKind::Number);
    return end - i;
}

int LineScanner::identifier(int i)
{
    if (!isIdentStart(t.at(i)))
        return 0;
    int j = i;
    while (j < n && isIdent(t.at(j)))
        ++j;
    const QString word = t.mid(i, j - i);

    // Prefixed string literals: r"", f"", b'', u8"", R"(…)", r#"…"#
    if (j < n && isStringPrefix(word, s.stringPrefixes)
        && (t.at(j) == QLatin1Char('"')
            || ((s.has(SingleQuote) || s.has(CharLiteral)) && t.at(j) == QLatin1Char('\'')))) {
        const int end = endOfQuoted(t, j, t.at(j), true);
        add(i, end - i, TokenKind::String);
        return end - i;
    }
    if (s.has(RustRaw) && word == QLatin1String("r") && j < n && t.at(j) == QLatin1Char('#')) {
        int hashes = 0;
        while (j + hashes < n && t.at(j + hashes) == QLatin1Char('#'))
            ++hashes;
        if (j + hashes < n && t.at(j + hashes) == QLatin1Char('"')) {
            const QString terminator = QLatin1Char('"') + QString(hashes, QLatin1Char('#'));
            const int close = t.indexOf(terminator, j + hashes + 1);
            const int end = close < 0 ? n : close + terminator.size();
            add(i, end - i, TokenKind::String);
            return end - i;
        }
    }

    const QString key = s.has(FoldCase) ? word.toLower() : word;
    if (s.keywords.contains(key)) {
        add(i, j - i, TokenKind::Keyword);
    } else if (s.types.contains(key)) {
        add(i, j - i, TokenKind::Type);
    } else {
        const QChar next = peekNonSpace(t, j);
        if (s.has(KeyBeforeColon) && next == QLatin1Char(':'))
            add(i, j - i, s.has(ColonIsTarget) ? TokenKind::Function : TokenKind::Type);
        else if (s.has(KeyBeforeEquals) && next == QLatin1Char('='))
            add(i, j - i, TokenKind::Type);
        else if (s.has(FunctionCalls) && next == QLatin1Char('('))
            add(i, j - i, TokenKind::Function);
    }
    return j - i;
}

void LineScanner::run()
{
    out.clear();
    if (n == 0)
        return;

    int i = resumeBlock();
    if (i >= n)
        return;

    while (firstNonSpace < n && t.at(firstNonSpace).isSpace())
        ++firstNonSpace;

    if (s.has(MarkdownText)) {
        scanMarkdown(i);
        return;
    }

    while (i < n) {
        if (t.at(i).isSpace()) {
            ++i;
            continue;
        }
        int length = comment(i);
        if (length == 0)
            length = hash(i);
        if (length == 0)
            length = sectionHeader(i);
        if (length == 0)
            length = xmlTag(i);
        if (length == 0)
            length = decorator(i);
        if (length == 0)
            length = dollarVar(i);
        if (length == 0)
            length = quoted(i);
        if (length == 0)
            length = number(i);
        if (length == 0)
            length = identifier(i);
        // Anything none of the steps recognises is one character of punctuation.
        i += qMax(1, length);
    }
}

void scanLine(const QString &t, const LangSpec &s, Block &state, QVector<SyntaxSpan> &out)
{
    LineScanner scanner(t, s, state, out);
    scanner.run();
}

} // namespace

// ------------------------------------------------------------------ the API

Language SyntaxHighlighter::languageFor(const QString &path, const DiffDocument *docForShebang)
{
    const LangTable &table = languageTable();
    const QFileInfo info(path);
    const QString name = info.fileName().toLower();
    if (table.byName.contains(name))
        return table.byName.value(name);
    if (name.startsWith(QLatin1String("dockerfile")))
        return Language::Dockerfile;
    if (name.startsWith(QLatin1String("makefile")))
        return Language::Make;
    const Language byExt = table.byExtension.value(info.suffix().toLower(), Language::None);
    if (byExt != Language::None)
        return byExt;

    // Nothing in the name: try the interpreter line of the first content line.
    if (docForShebang) {
        for (const DiffLine &l : docForShebang->lines) {
            if (l.state == DiffLine::Header)
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
    const LangSpec &spec = languageTable().specs[size_t(lang)];

    // The diff interleaves two files. `oldState` follows the base version
    // (context + removed lines), `newState` the working tree (context +
    // added lines), so an unterminated comment on one side cannot colour the
    // other side's lines.
    Block oldState = NoBlock, newState = NoBlock;
    QVector<SyntaxSpan> scratch;

    for (DiffLine &l : doc.lines) {
        switch (l.state) {
        case DiffLine::Header:
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
                // Not a no-op: the line may have opened or closed a block, and
                // a context line does that for both versions of the file.
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
