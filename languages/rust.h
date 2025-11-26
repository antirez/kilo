char *RUST_HL_extensions[];
char *RUST_HL_keywords[];

const struct editorSyntax syntax_rust = {
    RUST_HL_extensions,
    RUST_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
};

char *RUST_HL_extensions[] = {
    ".rs",
    NULL
};

char *RUST_HL_keywords[] = {

    /* Keywords */
    "as", "break", "const", "continue", "crate", "else",
    "enum", "extern", "false", "fn", "for", "if", "impl",
    "in", "let", "loop", "match", "mod", "move", "mut",
    "pub", "ref", "return", "self", "Self", "static",
    "struct", "super", "trait", "true", "type", "unsafe",
    "use", "where", "while", "async", "await", "dyn",

    /* Types */
    "i8|", "i16|", "i32|", "i64|", "i128|", "isize|",
    "u8|", "u16|", "u32|", "u64|", "u128|", "usize|",
    "f32|", "f64|",
    "bool|", "char|", "str|",
    "Option|", "Result|", "String|", "Vec|",

    NULL
};