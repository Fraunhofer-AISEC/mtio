#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#define ALLOCA alloca

// struct with two integers
typedef struct mystruct_t {
    char a[8];
    char b[8];
} mystruct;


int main(int argc, char * argv[])
{
    char * data;
    char * dataBadBuffer = (char *)new char[50];
    char * dataGoodBuffer = (char *)new char[100];
    /* FLAW: Set a pointer to a "small" buffer. This buffer will be used in the sinks as a destination
     * buffer in various memory copying functions using a "large" source buffer. */
    data = dataBadBuffer;
    data[0] = '\0'; /* null terminate */
    size_t i;
    char source[100];
    memset(source, 'C', 100-1); /* fill with 'C's */
    source[100-1] = '\0'; /* null terminate */

    // delete [] data;

    data[100-1] = '\0'; /* Ensure the destination buffer is null terminated */
}