/**
 * err_code_gen.c
 *
 * Tool to generate error codes
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main (int argc, const char *argv[])
{

    int i, min, max;
    time_t t;

    min = 100000000;
    max = 999999999;

    srand((unsigned) time(&t));
    srand((unsigned) rand());

    printf("\n\n%d\n\n", min + rand() % (max-min));

}
