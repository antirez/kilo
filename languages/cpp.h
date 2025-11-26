
char *CPP_HL_extensions[];
char *CPP_HL_keywords[];
const struct editorSyntax syntax_cpp = {
    CPP_HL_extensions,
    CPP_HL_keywords,
    "//","/*","*/",
    HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS  
};

char *CPP_HL_extensions[] = {".cpp",".hpp",".cc",NULL};
char *CPP_HL_keywords[] = {
    /* Preprocessor Keywords */
    "#define", "#include", "#pragma", "#undef", 
    "#if", "#ifdef", "#ifndef", "#else", "#elif", "#endif", "#error",

	/* C Keywords */
	"auto","break","case","continue","default","do","else","enum",
	"extern","for","goto","if","register","return","sizeof","static",
	"struct","switch","typedef","union","volatile","while","NULL",

	/* C++ Keywords */
	"alignas","alignof","and","and_eq","asm","bitand","bitor","class",
	"compl","constexpr","const_cast","deltype","delete","dynamic_cast",
	"explicit","export","false","friend","inline","mutable","namespace",
	"new","noexcept","not","not_eq","nullptr","operator","or","or_eq",
	"private","protected","public","reinterpret_cast","static_assert",
	"static_cast","template","this","thread_local","throw","true","try",
	"typeid","typename","virtual","xor","xor_eq",

	/* C types */
        "int|","long|","double|","float|","char|","unsigned|","signed|",
        "void|","short|","auto|","const|","bool|",NULL
};