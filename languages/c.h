char *C_HL_extensions[];
char *C_HL_keywords[];
const struct editorSyntax syntax_c = {
    C_HL_extensions,
    C_HL_keywords,
    "//","/*","*/",
    HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS  
};

char *C_HL_extensions[] = {".c",".h",NULL};
char *C_HL_keywords[] = {
    /* Preprocessor Keywords */
    "#define", "#include", "#pragma", "#undef", 
    "#if", "#ifdef", "#ifndef", "#else", "#elif", "#endif", "#error",

	/* C Keywords */
	"auto","break","case","continue","default","do","else","enum",
	"extern","for","goto","if","register","return","sizeof","static",
	"struct","switch","typedef","union","volatile","while","NULL",

	/* C types */
        "int|","long|","double|","float|","char|","unsigned|","signed|",
        "void|","short|","auto|","const|","bool|",NULL
};