/* 	
*   Separating out the syntaxes like this should make it much easier to add more later,
* 	and makes this portion of the code much easier to read, taking almost a hundred
*   lines of code out of the main program logic, bringing it a little closer to the
*   goal of being "less than 1000 SLOC"
*/

#include "asm.h"
#include "c.h"
#include "cpp.h"
#include "rust.h"
#include "python.h"

struct editorSyntax HLDB[] = {

	syntax_asm,
	syntax_c,
	syntax_cpp,
	syntax_rust,
	syntax_python

};
#define HLDB_ENTRIES (sizeof(HLDB)/sizeof(HLDB[0]))