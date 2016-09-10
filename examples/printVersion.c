// LZ5 trivial example : print Library version number
// Copyright : Takayuki Matsuoka & Yann Collet


#include <stdio.h>
#include "lz5_compress.h"

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
    printf("Hello World ! LZ5 Library version = %d\n", LZ5_versionNumber());
    return 0;
}
