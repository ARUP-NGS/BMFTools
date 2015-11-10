#ifndef BAM_PR_H
#define BAM_PR_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>
#include <unistd.h>
#include <tgmath.h>
#include <inttypes.h>
#include "htslib/sam.h"
#include "sam_opts.h"
#include "bam.h" // for bam_get_library
#include "bam_rescue.h"
#include "igamc_cephes.c"

#define STACK_START (1 << 5)

//Multiply a phred score by this to convert a -10log_10(x) to a -2log_e(x)
#define LOG10E_X5_INV 0.460517018598809136803598290936872841520220297725754595206665580193514521935470496
#define LOG10E_X5_1_2 0.230258509299404568401799145468436420760110148862877297603332790096757260967735248
//such as in the following macro:
#define LOG10_TO_CHI2(x) (x) * LOG10E_X5_INV


#ifndef bam_is_r1
#define bam_is_r1(b) (!!((b)->core.flag&BAM_FREAD1))
#endif

#ifndef INC_TAG
#define INC_TAG(p, b, key) *(int *)(bam_aux_get(p, key) + 1) += *(int *)(bam_aux_get(b, key) + 1);
#endif

#ifndef bam_is_r2
#define bam_is_r2(b) (!!((b)->core.flag&BAM_FREAD2))
#endif


#ifndef ucs_sort_mate_key
#define ucs_sort_mate_key(a) (uint64_t)((uint64_t)a->core.mtid<<32|(bam_aux2i(bam_aux_get(b, "MU")) + 1)<<2|(!!(a->core.flag & BAM_FMREVERSE)))
#endif

#ifndef ucs_sort_core_key
#define ucs_sort_core_key(a) (uint64_t)((uint64_t)a->core.tid<<32|(bam_aux2i(bam_aux_get(b, "SU"))+1)<<2|bam_is_rev(a)<<1|bam_is_r1(a))
#endif

#ifndef bam_sort_core_key
#define bam_sort_core_key(a) (uint64_t)((uint64_t)a->core.tid<<32|(a->core.pos+1)<<2|bam_is_rev(a)<<1|bam_is_r1(a))
#endif

#ifndef bam_sort_mate_key
#define bam_sort_mate_key(a) (uint64_t)((uint64_t)a->core.mtid<<32|(a->core.mpos+1)<<1|(!!(a->core.flag & BAM_FMREVERSE)))
#endif

#ifndef AVG_LOG_TO_CHI2
#define AVG_LOG_TO_CHI2(x) (x) * LOG10E_X5_1_2
#endif

typedef bam1_t *bam1_p;

typedef struct {
    int n, max;
    bam1_t **a;
} tmp_stack_t;

static inline void stack_insert(tmp_stack_t *stack, bam1_t *b)
{
    if (stack->n == stack->max) {
        stack->max = stack->max? stack->max<<1 : 0x10000;
        stack->a = (bam1_t**)realloc(stack->a, sizeof(bam1_t*) * stack->max);
    }
    stack->a[stack->n++] = b;
}

static inline void resize_stack(tmp_stack_t *stack, size_t n) {
	stack->max = n;
	stack->a = (bam1_t **)realloc(stack->a, sizeof(bam1_t *) * n);
#if !NDEBUG
	if(!stack->a) {
		fprintf(stderr, "Failed to reallocate memory for %i bam1_t * objects. Abort!\n");
		exit(EXIT_FAILURE);
	}
#endif
}


typedef struct pr_settings {
	FILE *fqh;
	samFile *out;
	int cmpkey; // 0 for pos, 1 for unclipped start position
	int mmthr; // Mismatch failure threshold.
	int annealed_check; // Set to true to check a reversed barcode for a mismatch limit.
} pr_settings_t;

static inline int barcmp(bam1_t *a, bam1_t *b, pr_settings_t *settings)
{
	char *aname = bam_get_qname(a);
	char *bname = bam_get_qname(b);
	int m = 0; // mismatches
	int lqn = a->core.l_qname - 1;
	for(int i = 0; i < lqn; ++i) {
		if(aname[i] != bname[i])
	}
}

static inline int pvalue_to_phred(double pvalue)
{
	return (int)(-10 * log10(pvalue) + 0.5); // Add 0.5 to round up
}

static inline int disc_pvalues(double pv_better, double pv_worse)
{
	return pvalue_to_phred(igamc(2., LOG10_TO_CHI2(pv_better - (10. * log10(1 - pow(10., (pv_worse * 0.1)))))));
}


// Converts a chi2 sum into a p value.
static inline double igamc_pvalues(int num_pvalues, double x)
{
	if(x < 0) {
		return 1.0;
	}
	else {
		return igamc((double)num_pvalues, x / 2.0);
	}
}

static inline void *array_tag(bam1_t *b, const char *tag) {
	uint8_t *data = bam_aux_get(b, tag);
#if !NDEBUG
	if(*data++ != 'B') {
		fprintf(stderr, "This is not an array tag. Abort mission! (%c)\n", *data);
		exit(EXIT_FAILURE);
	}
	char typecode = *data++;
	int n = *(((int *)data)++);
	return data ? (void *)(data): NULL; // Offset by 1 to skip typecode, 2 to skip array length.
#else
	return data ? (void *)(data + 6): NULL;
#endif
}

static inline void stack_flatten(tmp_stack_t *stack, pr_settings_t *settings) {
	bam1_t **a = stack->a;
	uint8_t *idata, *jdata;
	for(int i = 0; i < stack->n; ++i) {
		for(int j = i + 1; j < stack->n; ++j) {
			if(barcmp(a[i], a[j], settings)) {
				update_bam(a[j], a[i]); // Update j with i's information
				bam1_destroy(a[i]);
				a[i] = NULL;
			}
		}
	}
}

static inline void stack_resize(tmp_stack_t *stack, size_t n) {
    stack->a = (bam1_t **)realloc(stack->a, n * sizeof(tmp_stack_t)); // Default pre-allocate 32
    memset(&stack, 0, sizeof(tmp_stack_t) * n);
    return;
}

int bam_pr(int argc, char *argv[]);


#endif /* BAM_PR_H */