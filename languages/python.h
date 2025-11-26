
char *PYTHON_HL_extensions[];
char *PYTHON_HL_keywords[];
const struct editorSyntax syntax_python = {
    PYTHON_HL_extensions,
    PYTHON_HL_keywords,
    "# ","''","''",
    HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS  
};

char *PYTHON_HL_extensions[] = {".py",".pyx",".pyc", NULL};
char *PYTHON_HL_keywords[] = {

    /* Keywords */
    
    "False", "None", "True", "and", "as", "assert", "async", "await", "break", 
    "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", 
    "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",

    /* Class dunder methods and names */
    "self|",
    "__init__", "__new__", "__del__", "__enter__", "__exit__", "__copy__", "__deepcopy__",
    "__repr__", "__str__", "__bytes__", "__format__",
    "__getattr__", "__getattribute__", "__setattr__", "__delattr__", "__dir__",
    "__get__", "__set__", "__delete__",
    "__len__", "__getitem__", "__setitem__", "__delitem__", "__iter__", "__next__",
    "__contains__", "__reversed__", "__missing__",
    "__call__", "__await__", "__aiter__", "__anext__", "__aenter__", "__aexit__",
    "__neg__", "__pos__", "__abs__", "__invert__",
    "__add__", "__sub__", "__mul__", "__matmul__", "__truediv__", "__floordiv__",
    "__mod__", "__divmod__", "__pow__", "__lshift__", "__rshift__",
    "__and__", "__xor__", "__or__",
    "__radd__", "__rsub__", "__rmul__", "__rmatmul__", "__rtruediv__", "__rfloordiv__",
    "__rmod__", "__rdivmod__", "__rpow__", "__rlshift__", "__rrshift__",
    "__rand__", "__rxor__", "__ror__",
    "__iadd__", "__isub__", "__imul__", "__imatmul__", "__itruediv__", "__ifloordiv__",
    "__imod__", "__ipow__", "__ilshift__", "__irshift__", "__iand__", "__ixor__", "__ior__",
    "__eq__", "__ne__", "__lt__", "__le__", "__gt__", "__ge__", "__hash__",
    "__int__", "__float__", "__complex__", "__index__", "__bool__"

    /* Types */
    "object|", "bool|", "int|", "float|", "complex|", "str|", 
    "bytes|", "bytearray|", "list|", "tuple|", "set|", "frozenset|", "dict|", "range|",

    NULL

};