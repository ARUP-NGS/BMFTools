/*  bam_rescue.c -- duplicate read detection.

    Copyright (C) 2009, 2015 Genome Research Ltd.
    Portions copyright (C) 2009 Broad Institute.

    Author: Heng Li <lh3@sanger.ac.uk>

    Modified 9/15 by Daniel baker <daniel.baker@aruplab.com>, <d.nephi.baker@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.  */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>
#include <unistd.h>
#include "htslib/sam.h"
#include "sam_opts.h"
#include "bam.h" // for bam_get_library
#include "kingsfisher.h" // For igamc/igamc_pvalues

typedef bam1_t *bam1_p;

#include "htslib/khash.h"
KHASH_SET_INIT_STR(name)
KHASH_MAP_INIT_INT64(pos, bam1_p)
KHASH_MAP_INIT_STR(rescue, bam1_p)

#define BUFFER_SIZE 0x40000

typedef struct {
    uint64_t n_checked, n_removed;
    khash_t(pos) *best_hash;
} lib_aux_t;
KHASH_MAP_INIT_STR(lib, lib_aux_t)

typedef struct tmp_stack {
    int n, max;
    bam1_t **a;
} tmp_stack_t;


// arr1 and arr2 should each be a uint64_t buf[2] or a malloc'd char * of size 2.
static inline int arr_cmp(char *arr1, char *arr2) {
    return (arr1[0] == arr2[0] && arr1[1] == arr2[1]);
}

#ifndef ARR_CMP
#define ARR_CMP(arr1, arr2) (arr1[0] == arr2[0] && arr1[1] == arr2[1])
#endif


// buf should be a uint64_t buf[2] or a malloc'd char * of size 2.
#ifndef ARR_SETKEY
#define ARR_SETKEY(bam, buf) buf = {(uint64_t)(IS_REVERSE(bam) >> 59 | IS_MATE_REVERSE(bam) >> 57|                       \
                                               IS_READ1(bam) >> 55 | IS_READ2(bam) >> 53 |                               \
                                               bam->core.mtid >> 44 | bam->core.tid >> 28)                               \
                                    , (uint64_t)(bam->core.pos >> 32 | bam->core.mpos)}
#endif


static inline void stack_insert(tmp_stack_t *stack, bam1_t *b)
{
    if (stack->n == stack->max) {
        stack->max = stack->max? stack->max<<1 : 0x10000;
        stack->a = (bam1_t**)realloc(stack->a, sizeof(bam1_t*) * stack->max);
    }
    stack->a[stack->n++] = b;
}

static inline void write_stack(tmp_stack_t *stack, samFile *out, bam_hdr_t *hdr)
{
    int i;
    for (i = 0; i != stack->n; ++i) {
        if(stack->a[i]) { // Since I'll be clearing out the values after updating, I am checking to see if it's not NULL first.
            sam_write1(out, hdr, stack->a[i]);
            bam_destroy1(stack->a[i]);
        }
    }
    stack->n = 0;
}

static inline int hamming_dist_test(char *bs1, char *bs2, int len, int hd_thresh)
{
    int mm = 0;
    for(int i = 0; i < len; ++i) {
        if(bs1[i] != bs2[i]) {
            ++mm;
            if(!(mm - hd_thresh)) {
                return 1;
            }
        }
    }
    return 0;
}

static inline void update_bam1(bam1_t *p, bam1_t *b)
{
    int realign = 0;
    uint8_t *pPVptr = bam_aux_get(p, "PV");
    uint8_t *bPVptr = bam_aux_get(b, "PV");
#if !NDEBUG
    if(!bPVptr || !pPVptr) {
        fprintf(stderr, "Required PV tag not found. Abort mission!\n");
        exit(EXIT_FAILURE);
    }
#endif
    int *bPV = (int *)bPVptr; // Length of this should be b->l_qseq
    int *pPV = (int *)pPVptr; // Length of this should be p->l_qseq
    char *bSeq = (char *)bam_get_seq(b);
    char *pSeq = (char *)bam_get_seq(p);
    for(int i = 0; i < p->core.l_qseq; ++i) {
        if(bSeq[i] == pSeq[i]) {
            pPV[i] = igamc((double)2, LOG10_TO_CHI2(pPV[i] + bPV[i]) / 2.0);
        }
        else {
            if(pPV[i] > bPV[i]) {
                pPV[i] -= bPV[i];
            }
            else {
                pSeq[i] = bSeq[i];
                pPV[i] = bPV[i] - pPV[i];
                realign = 1;
            }
            if(pPV[i] < 3) {
                pSeq[i] = 'N';
                realign = 1;
            }
        }

        // TODO: Check for a merged tag.
        // Add if not present.
        if(realign) {
            // TODO: Check for a realign tag.
            fprintf(stderr, "Not implemented yet!\n");
            exit(EXIT_FAILURE);
            //bam_aux_append(b, "RA", )
        }
        bam_destroy1(b);
        b = NULL;
    }
}

static inline void flatten_stack(tmp_stack_t *stack, int len, int hd_thresh)
{
    int pass;
    bam1_t *b, bam1_t *p;
    for(int i = 0; i < stack->n, ++i) {
        b = stack->a[i];
        uint8_t *cb = bam_aux_get(b, "BS");
#if !NDEBUG
        pass = 1;
        if(!cb) {
            fprintf(stderr, "BS tag not found for read. Name: %s. Flag: %i.\n", bam_get_qname(b), b->core.flag);
            pass = 0;
        }
#endif
        for(int j = i + 1; j < stack->n; ++i) {
            p = stack->a[j];
            uint8_t *cp = bam_aux_get(p, "BS");
#if !NDEBUG
            if(!cp) {
                fprintf(stderr, "BS tag not found for read. Name: %s. Flag: %i.\n", bam_get_qname(p), p->core.flag);
                pass = 0;
            }
            if(!pass) {
                exit(EXIT_FAILURE);
            }
#endif

        }
    }
    if(hamming_dist_test(cp, cb, len, hd_thresh)) {
        update_bam1(p, b); // Update record p with b
    }
    return;
}

typedef struct rescue_settings {
    int len;
    int hd_thresh;
} rescue_settings_t;

void bam_rescue_core(samFile *in, bam_hdr_t *hdr, samFile *out, rescue_settings_t *settings)
{
    bam1_t *b, *tmpb;
    int last_tid = -1;
    tmp_stack_t stack;
    uint64_t last_arr[2] = {0, 0};
    uint64_t current_arr[2] = {0, 0};

    b = bam_init1();
    memset(&stack, 0, sizeof(tmp_stack_t));

    while (sam_read1(in, hdr, b) >= 0) {
        bam1_core_t *c = &b->core;
        ARR_SETKEY(b, current_arr);
        if (!ARR_CMP(last_arr, current_arr)) {
            flatten_stack(&stack, settings->len, settings->hd_thresh);
            write_stack(&stack, out, hdr); // write the result
                if ((int)c->tid == -1) { // append unmapped reads
                    sam_write1(out, hdr, b);
                    while (sam_read1(in, hdr, b) >= 0) sam_write1(out, hdr, b);
                    break;
                }
                if(last_tid != c->tid) {
                    last_tid = c->tid;
                    fprintf(stderr, "[bam_rescue_core] processing reference %s...\n", hdr->target_name[c->tid]);
                }
            }
        if (!(c->flag&BAM_FPAIRED)) {
            sam_write1(out, hdr, b);
        } else { // paired, head
            int pass = 1; // For a missing BS tag.
            /*
             * OKAY, NOW MY CODE
             */
                if (ret == 0)
                    fprintf(stderr, "[bam_rescue_core] inconsistent BAM file for pair '%s'. Continue anyway.\n", bam_get_qname(b));
            } else { // not found in best_hash
                tmpb = bam_dup1(b);
                stack_insert(&stack, tmpb);
            }
    }

    for (k = kh_begin(aux); k != kh_end(aux); ++k) {
        if (kh_exist(aux, k)) {
            lib_aux_t *q = &kh_val(aux, k);
            write_stack(&stack, out, hdr);
            fprintf(stderr, "[bam_rescue_core] %lld / %lld = %.4lf in library '%s'\n", (long long)q->n_removed,
                    (long long)q->n_checked, (double)q->n_removed/q->n_checked, kh_key(aux, k));
            kh_destroy(pos, q->best_hash);
            free((char*)kh_key(aux, k));
        }
    }
    kh_destroy(lib, aux);

    clear_del_set(del_set);
    kh_destroy(name, del_set);
    free(stack.a);
    bam_destroy1(b);
}

void bam_rescue_se_core(samFile *in, bam_hdr_t *hdr, samFile *out, int force_se)
{
    fprintf(stderr, "Single-end rescue not implemented. Abort mission! (Or you could code it -- let me know!)\n");
    exit(EXIT_FAILURE);
}

static int rescue_usage(void) {
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage:  samtools rescue [-sS] <input.srt.bam> <output.bam>\n\n");
    fprintf(stderr, "Option: -s    rescue for SE reads\n");
    fprintf(stderr, "        -S    treat PE reads as SE in rescue (force -s)\n");

    sam_global_opt_help(stderr, "-....");
    return 1;
}

int bam_rescue(int argc, char *argv[])
{
    int c, is_se = 0, force_se = 0;
    samFile *in, *out;
    bam_hdr_t *header;
    char wmode[3] = {'w', 'b', 0};
    sam_global_args ga = SAM_GLOBAL_ARGS_INIT;

    static const struct option lopts[] = {
        SAM_OPT_GLOBAL_OPTIONS('-', 0, 0, 0, 0),
        { NULL, 0, NULL, 0 }
    };

    while ((c = getopt_long(argc, argv, "sS", lopts, NULL)) >= 0) {
        switch (c) {
        case 's': is_se = 1; break;
        case 'S': force_se = is_se = 1; break;
        default:  if (parse_sam_global_opt(c, optarg, lopts, &ga) == 0) break;
            /* else fall-through */
        case '?': return rescue_usage();
        }
    }
    if (optind + 2 > argc)
        return rescue_usage();

    in = sam_open_format(argv[optind], "r", &ga.in);
    header = sam_hdr_read(in);
    if (header == NULL || header->n_targets == 0) {
        fprintf(stderr, "[bam_rescue] input SAM does not have header. Abort!\n");
        return 1;
    }

    sam_open_mode(wmode+1, argv[optind+1], NULL);
    out = sam_open_format(argv[optind+1], wmode, &ga.out);
    if (in == 0 || out == 0) {
        fprintf(stderr, "[bam_rescue] fail to read/write input files\n");
        return 1;
    }
    sam_hdr_write(out, header);

    if (is_se) bam_rescue_se_core(in, header, out, force_se);
    else bam_rescue_core(in, header, out);
    bam_hdr_destroy(header);
    sam_close(in); sam_close(out);
    return 0;
}
