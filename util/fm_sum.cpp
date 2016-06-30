#include "dlib/bam_util.h"
#include <getopt.h>

int usage(char **argv, int retcode=EXIT_FAILURE) {
    fprintf(stderr, "%s in.bam\n"
                    "Use - for stdin.\n", argv[0]);
    return retcode;
}

int main(int argc, char *argv[]) {
    if(argc < 2)
        return usage(argv);
    if(strcmp(argv[1], "--help") == 0) {
        return usage(argv, EXIT_SUCCESS);
    }
    int c;
    while((c = getopt(argc, argv, "l:h?")) > -1) {
        switch(c) {
        case 'h': case '?': return usage(argv, EXIT_SUCCESS);
        }
    }
    for(int i(1); i < argc; ++i) {
        dlib::BamHandle in(argv[i]);
        bam1_t *b(bam_init1());
        size_t count(0);
        while(sam_read1(in.fp, in.header, b) >= 0) count += bam_itag(b,"FM") * ((b->core.flag & 2304) == 0);
        fprintf(stdout, "%s: %lu\n", argv[i], count);
        bam_destroy1(b);
    }
    return EXIT_SUCCESS;
}