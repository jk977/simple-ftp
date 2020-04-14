#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

static char const* program;

void usage(FILE* stream)
{
    fprintf(stream, "Usage:\n");
    fprintf(stream, "\t%s -h\n", program);
    fprintf(stream, "\t%s [-d]\n\n", program);

    fprintf(stream, "Options:\n");
    fprintf(stream, "\t-h\tShow this help message and exit.\n");
    fprintf(stream, "\t-d\tEnable debug output.\n");
}

int main(int argc, char** argv)
{
    program = argv[0];

    int opt;

    while ((opt = getopt(argc, argv, "dh")) != -1) {
        switch (opt) {
        case 'd':
            break;
        case 'h':
            usage(stdout);
            return EXIT_SUCCESS;
        default:
        case '?':
            usage(stderr);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
