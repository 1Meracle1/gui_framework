#include "syntax.h"

#include <base/unicode.h>

namespace code_editor {

    [[nodiscard]] auto is_abap_word_start(char ch) -> bool {
        return is_ascii_alpha(ch) || ch == '_';
    }

    [[nodiscard]] auto is_abap_word_char(char ch) -> bool {
        return is_ascii_alphanumeric(ch) || ch == '_' || ch == '-';
    }

    [[nodiscard]] auto is_abap_keyword(StrRef token) -> bool {
        constexpr StrRef KEYWORDS[] = {
            "ABSTRACT",
            "ACCEPTING",
            "ADD",
            "ADD-CORRESPONDING",
            "ADJACENT",
            "ALIASES",
            "ALL",
            "ALPHA",
            "ANALYZER",
            "AND",
            "APPEND",
            "APPENDING",
            "AS",
            "ASCENDING",
            "ASSERT",
            "ASSIGN",
            "ASSIGNED",
            "ASSIGNING",
            "ASSOCIATION",
            "ASYNCHRONOUS",
            "AT",
            "AUTHORITY",
            "AUTHORITY-CHECK",
            "BACK",
            "BACKGROUND",
            "BADI",
            "BASE",
            "BEGIN",
            "BETWEEN",
            "BINARY",
            "BIT",
            "BLANK",
            "BLANKS",
            "BLOCK",
            "BOUND",
            "BREAK-POINT",
            "BY",
            "BYTE",
            "BYTE-CA",
            "BYTE-CN",
            "BYTE-CO",
            "BYTE-CS",
            "BYTE-NA",
            "BYTE-NS",
            "CALL",
            "CASE",
            "CAST",
            "CASTING",
            "CATCH",
            "CHANGE",
            "CHANGING",
            "CHANNELS",
            "CHARACTER",
            "CHECK",
            "CHECKBOX",
            "CIRCULAR",
            "CLASS",
            "CLASS-DATA",
            "CLASS-EVENTS",
            "CLASS-METHODS",
            "CLASS-POOL",
            "CLEANUP",
            "CLEAR",
            "CLOCK",
            "CLOSE",
            "CN",
            "CO",
            "COLLECT",
            "COMMENT",
            "COMMIT",
            "COMMON",
            "COMMUNICATION",
            "COMPARING",
            "COMPONENT",
            "COMPONENTS",
            "COMPUTE",
            "CONCATENATE",
            "COND",
            "CONDENSE",
            "CONNECTION",
            "CONSTANTS",
            "CONTEXTS",
            "CONTINUE",
            "CONTROL",
            "CONTROLS",
            "CONV",
            "CONVERT",
            "COPY",
            "CORRESPONDING",
            "COUNTRY",
            "CP",
            "CREATE",
            "CS",
            "CURRENT",
            "CURSOR",
            "CUSTOMER-FUNCTION",
            "DATA",
            "DATABASE",
            "DATASET",
            "DATE",
            "DECIMALS",
            "DEFAULT",
            "DEFERRED",
            "DEFINE",
            "DEFINITION",
            "DELETE",
            "DELETING",
            "DEMAND",
            "DESCENDING",
            "DESCRIBE",
            "DESTINATION",
            "DETAIL",
            "DIALOG",
            "DIRECTORY",
            "DISPLAY",
            "DISPLAY-MODE",
            "DISTANCE",
            "DISTINCT",
            "DIVIDE",
            "DIVIDE-CORRESPONDING",
            "DO",
            "DUPLICATE",
            "DUPLICATES",
            "DURATION",
            "DYNAMIC",
            "DYNPRO",
            "EDITOR-CALL",
            "ELSE",
            "ELSEIF",
            "EMPTY",
            "ENABLED",
            "END",
            "END-ENHANCEMENT-SECTION",
            "END-LINES",
            "END-OF-DEFINITION",
            "END-OF-EDITING",
            "END-OF-FILE",
            "END-OF-PAGE",
            "END-OF-SELECTION",
            "END-TEST-INJECTION",
            "END-TEST-SEAM",
            "ENDAT",
            "ENDCASE",
            "ENDCATCH",
            "ENDCLASS",
            "ENDDO",
            "ENDENHANCEMENT",
            "ENDEXEC",
            "ENDFORM",
            "ENDFUNCTION",
            "ENDIF",
            "ENDINTERFACE",
            "ENDLOOP",
            "ENDMETHOD",
            "ENDMODULE",
            "ENDON",
            "ENDPROVIDE",
            "ENDSELECT",
            "ENDTRY",
            "ENDWHILE",
            "ENDWITH",
            "ENHANCEMENT",
            "ENHANCEMENT-POINT",
            "ENHANCEMENT-SECTION",
            "ENTRIES",
            "ENUM",
            "EQ",
            "EVENT",
            "EVENTS",
            "EXACT",
            "EXCEPT",
            "EXCEPTION",
            "EXCEPTIONS",
            "EXCLUDE",
            "EXEC",
            "EXIT",
            "EXIT-COMMAND",
            "EXPORT",
            "EXPORTING",
            "EXTENDED",
            "EXTENSION",
            "EXTRACT",
            "FETCH",
            "FIELD",
            "FIELD-GROUPS",
            "FIELD-SYMBOL",
            "FIELD-SYMBOLS",
            "FIELDS",
            "FILTER",
            "FILTERS",
            "FINAL",
            "FIND",
            "FIRST",
            "FIRST-LINE",
            "FOR",
            "FORM",
            "FORMAT",
            "FOUND",
            "FRAME",
            "FREE",
            "FRIENDS",
            "FROM",
            "FUNCTION",
            "FUNCTION-POOL",
            "GAPS",
            "GE",
            "GENERATE",
            "GET",
            "GLOBAL",
            "GROUP",
            "GROUPING",
            "GROUPS",
            "GT",
            "HANDLE",
            "HANDLER",
            "HARMLESS",
            "HASHED",
            "HEADER",
            "HEADING",
            "HIDE",
            "HOLD",
            "ID",
            "IF",
            "IGNORING",
            "IMPLEMENTATION",
            "IMPORT",
            "IMPORTING",
            "IN",
            "INCLUDE",
            "INCLUDING",
            "INDEX",
            "INDEX-LINE",
            "INFOTYPES",
            "INHERITING",
            "INIT",
            "INITIAL",
            "INITIALIZATION",
            "INNER",
            "INPUT",
            "INSERT",
            "INSTANCE",
            "INTERFACE",
            "INTERFACE-POOL",
            "INTERFACES",
            "INTERVALS",
            "INTO",
            "IS",
            "JOB",
            "JOIN",
            "KEEPING",
            "KEY",
            "KEYS",
            "LANGUAGE",
            "LE",
            "LEADING",
            "LEAVE",
            "LEFT",
            "LEFT-JUSTIFIED",
            "LENGTH",
            "LET",
            "LEVEL",
            "LIKE",
            "LINE",
            "LINE-SELECTION",
            "LINE-SIZE",
            "LINES",
            "LIST",
            "LIST-PROCESSING",
            "LISTBOX",
            "LOAD",
            "LOAD-OF-PROGRAM",
            "LOCAL",
            "LOCALE",
            "LOG",
            "LOG-POINT",
            "LOOP",
            "LOWER",
            "LT",
            "MAPPING",
            "MARGIN",
            "MATCH",
            "MATCHCODE",
            "MAXIMUM",
            "MEMORY",
            "MESH",
            "MESSAGE",
            "MESSAGING",
            "METHOD",
            "METHODS",
            "MINIMUM",
            "MODE",
            "MODIFY",
            "MODULE",
            "MOVE",
            "MOVE-CORRESPONDING",
            "MULTIPLY",
            "MULTIPLY-CORRESPONDING",
            "NE",
            "NEW",
            "NEW-LINE",
            "NEW-PAGE",
            "NEW-SECTION",
            "NEXT",
            "NO",
            "NO-DISPLAY",
            "NO-EXTENSION",
            "NO-GAP",
            "NO-GAPS",
            "NO-GROUPING",
            "NO-HEADING",
            "NO-SCROLLING",
            "NO-SIGN",
            "NO-TITLE",
            "NO-TOPOFPAGE",
            "NO-ZERO",
            "NODES",
            "NON-UNICODE",
            "NON-UNIQUE",
            "NOT",
            "NULL",
            "NULLS",
            "NUMBER",
            "OBJECT",
            "OBLIGATORY",
            "OCCURRENCE",
            "OCCURRENCES",
            "OCCURENCES",
            "OCCURS",
            "OF",
            "OFF",
            "OFFSET",
            "OLE",
            "ON",
            "OPEN",
            "OPTIONAL",
            "OPTIONS",
            "OR",
            "ORDER",
            "OTHERS",
            "OUT",
            "OUTER",
            "OUTPUT",
            "OUTPUT-LENGTH",
            "OVERLAY",
            "PACK",
            "PACKAGE",
            "PAGE",
            "PARAMETER",
            "PARAMETERS",
            "PART",
            "PERCENTAGE",
            "PERFORM",
            "PF",
            "PF-STATUS",
            "PLACES",
            "POOL",
            "POSITION",
            "PRIMARY",
            "PRINT",
            "PRINT-CONTROL",
            "PRIVATE",
            "PROCEDURE",
            "PROCESSING",
            "PROGRAM",
            "PROPERTY",
            "PROTECTED",
            "PROVIDE",
            "PUBLIC",
            "PUSH",
            "PUSHBUTTON",
            "PUT",
            "RADIOBUTTON",
            "RAISE",
            "RAISING",
            "RANGE",
            "RANGES",
            "READ",
            "RECEIVE",
            "RECEIVING",
            "REDEFINITION",
            "REDUCE",
            "REF",
            "REFERENCE",
            "REFRESH",
            "REGEX",
            "REJECT",
            "REPLACE",
            "REPORT",
            "REQUESTED",
            "RESERVE",
            "RESOLUTION",
            "RESPECTING",
            "RESULT",
            "RESULTS",
            "RESUMABLE",
            "RESUME",
            "RETRY",
            "RETURN",
            "RETURNING",
            "RFC",
            "RIGHT-JUSTIFIED",
            "RISK",
            "ROLLBACK",
            "ROWS",
            "RUN",
            "SCAN",
            "SCREEN",
            "SCROLL",
            "SCROLL-BOUNDARY",
            "SEARCH",
            "SECONDS",
            "SECTION",
            "SELECT",
            "SELECT-OPTIONS",
            "SELECTION",
            "SELECTION-SCREEN",
            "SELECTIONS",
            "SEPARATED",
            "SET",
            "SHARED",
            "SHIFT",
            "SHORT",
            "SHORTDUMP",
            "SIGN",
            "SINGLE",
            "SIZE",
            "SKIP",
            "SORT",
            "SORTED",
            "SOURCE",
            "SPLIT",
            "SQL",
            "STABLE",
            "STAMP",
            "STANDARD",
            "START",
            "START-OF-EDITING",
            "START-OF-SELECTION",
            "STARTING",
            "STATICS",
            "STATUS",
            "STEP-LOOP",
            "STOP",
            "STRUCTURE",
            "SUBMATCHES",
            "SUBMIT",
            "SUBROUTINE",
            "SUBSCREEN",
            "SUBTRACT",
            "SUBTRACT-CORRESPONDING",
            "SUM",
            "SUMMARY",
            "SUMMING",
            "SUPPLIED",
            "SUPPLY",
            "SUPPRESS",
            "SWITCH",
            "SYMBOL",
            "SYNTAX",
            "SYNTAX-CHECK",
            "SYNTAX-TRACE",
            "SYSTEM",
            "SYSTEM-CALL",
            "SYSTEM-EXCEPTIONS",
            "SYSTEM-EXIT",
            "TAB",
            "TABBED",
            "TABLE",
            "TABLES",
            "TABSTRIP",
            "TARGET",
            "TASK",
            "TASKS",
            "TEST",
            "TEST-INJECTION",
            "TEST-SEAM",
            "TESTING",
            "TEXT",
            "TEXTPOOL",
            "THEN",
            "TIME",
            "TIMES",
            "TITLE",
            "TITLE-LINES",
            "TITLEBAR",
            "TO",
            "TOP-OF-PAGE",
            "TRAILING",
            "TRANSACTION",
            "TRANSFER",
            "TRANSFORMATION",
            "TRANSLATE",
            "TRANSPORTING",
            "TRUNCATE",
            "TRY",
            "TYPE",
            "TYPE-POOL",
            "TYPE-POOLS",
            "TYPES",
            "ULINE",
            "UNASSIGN",
            "UNION",
            "UNIQUE",
            "UNPACK",
            "UNTIL",
            "UP",
            "UPDATE",
            "UPPER",
            "USER",
            "USER-COMMAND",
            "USING",
            "UTCLONG",
            "VALUE",
            "VALUES",
            "VERSION",
            "VIA",
            "VISIBLE",
            "WAIT",
            "WHEN",
            "WHERE",
            "WHILE",
            "WINDOW",
            "WITH",
            "WITH-HEADING",
            "WITH-TITLE",
            "WORK",
            "WRITE",
            "XML",
            "ZONE",
        };
        for (StrRef keyword : KEYWORDS) {
            if (token.equals_ignore_ascii_case(keyword)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto is_abap_type_keyword(StrRef token) -> bool {
        constexpr StrRef TYPES[] = {
            "ANY",    "C",      "CLIKE", "CSEQUENCE", "D",       "DECFLOAT16", "DECFLOAT34", "F",
            "I",      "INT1",   "INT2",  "INT4",      "INT8",    "N",          "NUMERIC",    "P",
            "SIMPLE", "STRING", "X",     "XSEQUENCE", "XSTRING", "ABAP_BOOL",
        };
        for (StrRef type : TYPES) {
            if (token.equals_ignore_ascii_case(type)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto abap_quoted_end(StrRef line, size_t index, char quote) -> size_t {
        size_t end = index + 1u;
        while (end < line.size()) {
            if (line[end] == quote) {
                if (end + 1u < line.size() && line[end + 1u] == quote) {
                    end += 2u;
                } else {
                    ++end;
                    break;
                }
            } else {
                ++end;
            }
        }
        return end;
    }

    [[nodiscard]] auto abap_template_end(StrRef line, size_t index) -> size_t {
        size_t end = index + 1u;
        while (end < line.size()) {
            if (line[end] == '\\' && end + 1u < line.size()) {
                end += 2u;
            } else if (line[end] == '|') {
                ++end;
                break;
            } else {
                ++end;
            }
        }
        return end;
    }

    [[nodiscard]] auto abap_number_end(StrRef line, size_t index) -> size_t {
        size_t end = index;
        while (end < line.size() && is_ascii_digit(line[end])) {
            ++end;
        }
        if (end + 1u < line.size() && line[end] == '.' && is_ascii_digit(line[end + 1u])) {
            end += 2u;
            while (end < line.size() && is_ascii_digit(line[end])) {
                ++end;
            }
        }
        if (end + 1u < line.size() && (line[end] == 'e' || line[end] == 'E')) {
            size_t exponent = end + 1u;
            if (line[exponent] == '+' || line[exponent] == '-') {
                ++exponent;
            }
            if (exponent < line.size() && is_ascii_digit(line[exponent])) {
                end = exponent + 1u;
                while (end < line.size() && is_ascii_digit(line[end])) {
                    ++end;
                }
            }
        }
        return end;
    }

    [[nodiscard]] auto abap_next_token(StrRef line, size_t index) -> SyntaxToken {
        char const ch = line[index];
        uint8_t const byte = static_cast<uint8_t>(ch);
        size_t end = index + 1u;
        SyntaxTokenKind kind = SyntaxTokenKind::TEXT;
        if (byte >= 0x80u) {
            size_t const size = base::utf8_codepoint_size(line, index);
            end = index + (size != 0u ? size : 1u);
        } else if (ch == ' ' || ch == '\t') {
            while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) {
                ++end;
            }
        } else if ((index == 0u && ch == '*') || ch == '"') {
            end = line.size();
            kind = SyntaxTokenKind::COMMENT;
        } else if (ch == '\'' || ch == '`') {
            end = abap_quoted_end(line, index, ch);
            kind = SyntaxTokenKind::STRING;
        } else if (ch == '|') {
            end = abap_template_end(line, index);
            kind = SyntaxTokenKind::STRING;
        } else if (is_ascii_digit(ch)) {
            end = abap_number_end(line, index);
            kind = SyntaxTokenKind::NUMBER;
        } else if (is_abap_word_start(ch)) {
            while (end < line.size() && is_abap_word_char(line[end])) {
                ++end;
            }
            StrRef const token(line.data() + index, end - index);
            if (is_abap_keyword(token)) {
                kind = SyntaxTokenKind::KEYWORD;
            } else if (is_abap_type_keyword(token)) {
                kind = SyntaxTokenKind::TYPE;
            }
        } else {
            kind = SyntaxTokenKind::PUNCTUATION;
        }
        return {.kind = kind, .start = index, .end = end};
    }

    [[nodiscard]] auto abap_syntax_tokenizer() -> SyntaxTokenizer {
        return {.next_token = abap_next_token, .match_pairs = true};
    }

} // namespace code_editor
