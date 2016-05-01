#include "bmf_infer.h"

#include <getopt.h>
#include "dlib/sort_util.h"
#include "lib/kingfisher.h"


namespace BMF {

    CONST static inline int same_infer_pos_se(bam1_t *b, bam1_t *p)
    {
        return bmfsort_se_key(b) == bmfsort_se_key(p) &&
                b->core.l_qseq == b->core.l_qseq;
    }

    CONST static inline int same_infer_ucs_se(bam1_t *b, bam1_t *p)
    {
        return ucs_se_sort_key(b) == ucs_se_sort_key(p)  &&
                b->core.l_qseq == b->core.l_qseq;
    }

    CONST static inline int same_infer_pos(bam1_t *b, bam1_t *p)
    {
        return (bmfsort_core_key(b) == bmfsort_core_key(p) &&
                bmfsort_mate_key(b) == bmfsort_mate_key(p) &&
                sort_rlen_key(b) == sort_rlen_key(p));
    }

    CONST static inline int same_infer_ucs(bam1_t *b, bam1_t *p)
    {
    #if !NDEBUG
        if(ucs_sort_core_key(b) == ucs_sort_core_key(p) &&
           ucs_sort_mate_key(b) == ucs_sort_mate_key(p)) {
            assert(b->core.tid == p->core.tid);
            assert(b->core.mtid == p->core.mtid);
            assert(b->core.mtid == p->core.mtid);
            assert(bam_itag(b, "MU") == bam_itag(p, "MU"));
            //assert(bam_itag(b, "SU") == bam_itag(p, "SU"));
            return 1;
        }
        return 0;
    #else
        return (ucs_sort_core_key(b) == ucs_sort_core_key(p) &&
                ucs_sort_mate_key(b) == ucs_sort_mate_key(p) &&
                sort_rlen_key(b) == sort_rlen_key(p));
    #endif
    }

    std::string BamFisherSet::to_fastq() {
        size_t l_name = name.l;
        int i, argmaxret, posdata;
        kstring_t seq = {0, (size_t)len + 1, (char *)malloc((len + 1) * sizeof(char))};
        std::vector<uint32_t> agrees;
        std::vector<uint32_t> full_quals; // igamc calculated
        agrees.reserve(len);
        full_quals.reserve(len);
        for(i = 0; i < len; ++i) {
            argmaxret = arr_max_u32(phred_sums.data(), i); // 0,1,2,3,4/A,C,G,T,N
            posdata = i * 5 + argmaxret;
            agrees.push_back(votes[posdata]);
            full_quals.push_back(pvalue_to_phred(igamc_pvalues(n, LOG10_TO_CHI2(phred_sums[posdata]))));
            // Mask unconfident base calls
            if(full_quals[i] < 2 || (double)agrees[i] / n < MIN_FRAC_AGREED) {
                seq.s[i] = 'N';
                max_observed_phreds[i] = '#';
            } else {
                seq.s[i] = num2nuc(argmaxret);
                // Since we can guarantee posdata will be >= i, it's safe to do this.
                max_observed_phreds[i] = max_observed_phreds[posdata] + 33;
            }
        }
        max_observed_phreds[i] = '\0';
        seq.s[i] = '\0';
        seq.l = i + 1;
        kstring_t faks = {0, 5uL * len, (char *)malloc(5 * len)};
        kputsnl(" PV:B:I", &name);
        for(i = 0; i < len; ++i) {
            ksprintf(&name, ",%u", full_quals[i]);
            ksprintf(&faks, ",%u", agrees[i]);
        }
        // 32 is for "\n+\n" + "\n" + "FP:i:1\tRV:i:0\n" + "\t" + "\t" + "FM:i:[Up to four digits]"
        //LOG_DEBUG("Name: %s.\n", name.c_str());
        kputsnl("\tFA:B:I", &name);
        kputsn(faks.s, faks.l, &name);
        ksprintf(&name, "\tFM:i:%u\t", n);
        kputsnl("FP:i:1\tRV:i:0\n", &name);
        ksprintf(&name, "%s\n+\n%s\n", seq.s, max_observed_phreds);
        std::string ret = name.s;
        name.l = l_name, name.s[name.l] = '\0';
        free(faks.s), free(seq.s);
        return ret;
    }
    void BamFisherKing::add_to_hash(infer_aux_t *settings) {
        for(auto&& set: sets) {
            auto found = settings->realign_pairs.find(set.second.name.s);
            if(found == settings->realign_pairs.end()) {
                settings->realign_pairs.emplace(set.second.name.s, set.second.to_fastq());
            } else {
                assert(memcmp(found->second.c_str() + 1,
                              set.second.name.s,
                              set.second.name.l) == 0);
                if(set.second.get_is_read1()) {
                    fputs(set.second.to_fastq().c_str(), settings->fqh);
                    fputs(found->second.c_str(), settings->fqh);
                } else {
                    fputs(found->second.c_str(), settings->fqh);
                    fputs(set.second.to_fastq().c_str(), settings->fqh);
                }
            }
        }
    }

    /* OUTLINE FOR INFER
     * 1. multidimensional array (in 1-d) of nucleotide counts.
     * 2. multidimensional array (in 1-d) of phred sums.
     * 3. argmax
     * 4. Write as fastq.
     * 5. ???
     * 6. PROFIT
     *
     * */

    static const std::function<int (bam1_t *, bam1_t *)> fns[4] = {&same_infer_pos, &same_infer_pos_se,
                                                                   &same_infer_ucs, &same_infer_ucs_se};

    inline void bam2ffq(bam1_t *b, FILE *fp)
    {
        char *qual, *seqbuf;
        int i;
        uint8_t *seq, *rvdata;
        uint32_t *pv, *fa;
        int8_t t;
        kstring_t ks = {0, 0, nullptr};
        ksprintf(&ks, "@%s PV:B:I", bam_get_qname(b));
        pv = (uint32_t *)dlib::array_tag(b, "PV");
        fa = (uint32_t *)dlib::array_tag(b, "FA");
        for(i = 0; i < b->core.l_qseq; ++i) ksprintf(&ks, ",%u", pv[i]);
        kputs("\tFA:B:I", &ks);
        for(i = 0; i < b->core.l_qseq; ++i) ksprintf(&ks, ",%u", fa[i]);
        ksprintf(&ks, "\tFM:i:%i",
                bam_itag(b, "FM"));
        if((rvdata = bam_aux_get(b, "FP")) != nullptr)
            ksprintf(&ks, "\tFP:i:%i", bam_aux2i(rvdata));
        else kputs("\tFP:i:1", &ks);
        if((rvdata = bam_aux_get(b, "NC")) != nullptr)
            ksprintf(&ks, "\tNC:i:%i", bam_aux2i(rvdata));
        if((rvdata = bam_aux_get(b, "RV")) != nullptr)
            ksprintf(&ks, "\tRV:i:%i", bam_aux2i(rvdata));
        kputc('\n', &ks);
        seq = bam_get_seq(b);
        seqbuf = (char *)malloc(b->core.l_qseq + 1);
        for (i = 0; i < b->core.l_qseq; ++i) seqbuf[i] = seq_nt16_str[bam_seqi(seq, i)];
        seqbuf[i] = '\0';
        if (b->core.flag & BAM_FREVERSE) { // reverse complement
            for(i = 0; i < b->core.l_qseq>>1; ++i) {
                t = seqbuf[b->core.l_qseq - i - 1];
                seqbuf[b->core.l_qseq - i - 1] = nuc_cmpl(seqbuf[i]);
                seqbuf[i] = nuc_cmpl(t);
            }
            if(b->core.l_qseq&1) seqbuf[i] = nuc_cmpl(seqbuf[i]);
        }
        seqbuf[b->core.l_qseq] = '\0';
        assert(strlen(seqbuf) == (uint64_t)b->core.l_qseq);
        kputs(seqbuf, &ks);
        kputs("\n+\n", &ks);
        qual = (char *)bam_get_qual(b);
        for(i = 0; i < b->core.l_qseq; ++i) seqbuf[i] = 33 + qual[i];
        if (b->core.flag & BAM_FREVERSE) { // reverse
            for (i = 0; i < b->core.l_qseq>>1; ++i) {
                t = seqbuf[b->core.l_qseq - 1 - i];
                seqbuf[b->core.l_qseq - 1 - i] = seqbuf[i];
                seqbuf[i] = t;
            }
        }
        kputs(seqbuf, &ks), free(seqbuf);
        kputc('\n', &ks);
        fputs(ks.s, fp), free(ks.s);
    }

    inline int switch_names(char *n1, char *n2) {
        for(;*n1;++n1, ++n2)
            if(*n1 != *n2)
                return *n1 < *n2;
        return 0; // If identical, don't switch. Should never happen.
    }

    static inline void update_bam1(bam1_t *p, bam1_t *b)
    {
        uint8_t *bdata, *pdata;
        int n_changed;
        bdata = bam_aux_get(b, "FM");
        pdata = bam_aux_get(p, "FM");
        if(UNLIKELY(!bdata || !pdata)) {
            fprintf(stderr, "Required FM tag not found. Abort mission!\n");
            exit(EXIT_FAILURE);
        }
        int bFM = bam_aux2i(bdata);
        int pFM = bam_aux2i(pdata);
        if(switch_names(bam_get_qname(p), bam_get_qname(b))) {
            memcpy(bam_get_qname(p), bam_get_qname(b), b->core.l_qname);
            assert(strlen(bam_get_qname(p)) == strlen(bam_get_qname(b)));
        }
        pFM += bFM;
        bam_aux_del(p, pdata);
        bam_aux_append(p, "FM", 'i', sizeof(int), (uint8_t *)&pFM);
        if((pdata = bam_aux_get(p, "RV")) != nullptr) {
            const int pRV = bam_aux2i(pdata) + bam_itag(b, "RV");
            bam_aux_del(p, pdata);
            bam_aux_append(p, "RV", 'i', sizeof(int), (uint8_t *)&pRV);
        }
        // Handle NC (Number Changed) tag
        pdata = bam_aux_get(p, "NC");
        bdata = bam_aux_get(b, "NC");
        n_changed = dlib::int_tag_zero(pdata) + dlib::int_tag_zero(bdata);
        if(pdata) bam_aux_del(p, pdata);

        uint32_t *const bPV = (uint32_t *)dlib::array_tag(b, "PV"); // Length of this should be b->l_qseq
        uint32_t *const pPV = (uint32_t *)dlib::array_tag(p, "PV"); // Length of this should be b->l_qseq
        uint32_t *const bFA = (uint32_t *)dlib::array_tag(b, "FA"); // Length of this should be b->l_qseq
        uint32_t *const pFA = (uint32_t *)dlib::array_tag(p, "FA"); // Length of this should be b->l_qseq
        uint8_t *const bSeq = (uint8_t *)bam_get_seq(b);
        uint8_t *const pSeq = (uint8_t *)bam_get_seq(p);
        uint8_t *const bQual = (uint8_t *)bam_get_qual(b);
        uint8_t *const pQual = (uint8_t *)bam_get_qual(p);
        const int qlen = p->core.l_qseq;

        if(p->core.flag & (BAM_FREVERSE)) {
            int qleni1;
            int8_t ps, bs;
            for(int i = 0; i < qlen; ++i) {
                qleni1 = qlen - i - 1;
                ps = bam_seqi(pSeq, qleni1);
                bs = bam_seqi(bSeq, qleni1);
                if(ps == bs) {
                    pPV[i] = agreed_pvalues(pPV[i], bPV[i]);
                    pFA[i] += bFA[i];
                    if(bQual[qleni1] > pQual[qleni1]) pQual[qleni1] = bQual[qleni1];
                } else if(ps == dlib::htseq::HTS_N) {
                    bam_set_base(pSeq, bSeq, qleni1);
                    pFA[i] = bFA[i];
                    pPV[i] = bPV[i];
                    pQual[qleni1] = bQual[qleni1];
                    ++n_changed; // Note: goes from N to a useable nucleotide.
                    continue;
                } else if(bs == dlib::htseq::HTS_N) {
                    continue;
                } else {
                    if(pPV[i] > bPV[i]) {
                        bam_set_base(pSeq, bSeq, qleni1);
                        pPV[i] = disc_pvalues(pPV[i], bPV[i]);
                    } else pPV[i] = disc_pvalues(bPV[i], pPV[i]);
                    pFA[i] = bFA[i];
                    pQual[qleni1] = bQual[qleni1];
                    ++n_changed;
                }
                if(pPV[i] < 3) {
                    pFA[i] = 0;
                    pPV[i] = 0;
                    pQual[qleni1] = 2;
                    n_base(pSeq, qleni1);
                    continue;
                }
                if((uint32_t)(pQual[qleni1]) > pPV[i]) pQual[qleni1] = (uint8_t)pPV[i];
            }
        } else {
            int8_t ps, bs;
            for(int i = 0; i < qlen; ++i) {
                ps = bam_seqi(pSeq, i);
                bs = bam_seqi(bSeq, i);
                if(ps == bs) {
                    pPV[i] = agreed_pvalues(pPV[i], bPV[i]);
                    pFA[i] += bFA[i];
                    if(bQual[i] > pQual[i]) pQual[i] = bQual[i];
                } else if(ps == dlib::htseq::HTS_N) {
                    bam_set_base(pSeq, bSeq, i);
                    pFA[i] = bFA[i];
                    pPV[i] = bPV[i];
                    ++n_changed; // Note: goes from N to a useable nucleotide.
                    continue;
                } else if(bs == dlib::htseq::HTS_N) {
                    continue;
                } else {
                    if(pPV[i] > bPV[i]) {
                        bam_set_base(pSeq, bSeq, i);
                        pPV[i] = disc_pvalues(pPV[i], bPV[i]);
                    } else pPV[i] = disc_pvalues(bPV[i], pPV[i]);
                    pFA[i] = bFA[i];
                    pQual[i] = bQual[i];
                    ++n_changed;
                }
                if(pPV[i] < 3) {
                    pFA[i] = 0;
                    pPV[i] = 0;
                    pQual[i] = 2;
                    n_base(pSeq, i);
                    continue;
                } else if((uint32_t)(pQual[i]) > pPV[i]) pQual[i] = (uint8_t)pPV[i];
            }
        }
        bam_aux_append(p, "NC", 'i', sizeof(int), (uint8_t *)&n_changed);
    }


    void write_stack(dlib::tmp_stack_t *stack, infer_aux_t *settings)
    {
        for(unsigned i = 0; i < stack->n; ++i) {
            if(stack->a[i]) {
                uint8_t *data;
                std::string qname;
                if((data = bam_aux_get(stack->a[i], "NC")) != nullptr) {
                    qname = bam_get_qname(stack->a[i]);
                    if(settings->realign_pairs.find(qname) == settings->realign_pairs.end()) {
                        settings->realign_pairs[qname] = dlib::bam2cppstr(stack->a[i]);
                    } else {
                        // Make sure the read names/barcodes match.
                        assert(memcmp(settings->realign_pairs[qname].c_str() + 1, qname.c_str(), qname.size()) == 0);
                        // Write read 1 out first.
                        if(stack->a[i]->core.flag & BAM_FREAD2) {
                            fputs(settings->realign_pairs[qname].c_str(), settings->fqh);
                            bam2ffq(stack->a[i], settings->fqh);
                        } else {
                            bam2ffq(stack->a[i], settings->fqh);
                            fputs(settings->realign_pairs[qname].c_str(), settings->fqh);
                        }
                        // Clear entry, as there can only be two.
                        settings->realign_pairs.erase(qname);
                    }
                } else {
                    if(settings->write_supp && (bam_aux_get(stack->a[i], "SA") || bam_aux_get(stack->a[i], "ms"))) {
                        qname = bam_get_qname(stack->a[i]);
                        // Make sure the read names/barcodes match.
                        assert(memcmp(settings->realign_pairs[qname].c_str() + 1, qname.c_str(), qname.size()) == 0);
                        // Write read 1 out first.
                        if(stack->a[i]->core.flag & BAM_FREAD2) {
                            fputs(settings->realign_pairs[qname].c_str(), settings->fqh);
                            bam2ffq(stack->a[i], settings->fqh);
                        } else {
                            bam2ffq(stack->a[i], settings->fqh);
                            fputs(settings->realign_pairs[qname].c_str(), settings->fqh);
                        }
                        // Clear entry, as there can only be two.
                        settings->realign_pairs.erase(qname);
                    } else sam_write1(settings->out, settings->hdr, stack->a[i]);
                }
                bam_destroy1(stack->a[i]), stack->a[i] = nullptr;
            }
        }
    }

    static inline void flatten_stack_linear(dlib::tmp_stack_t *stack, infer_aux_t *settings)
    {
        BamFisherKing king(stack);
        king.add_to_hash(settings);
    }

    static const char *sorted_order_strings[2] = {"positional_rescue", "unclipped_rescue"};

    void infer_core(infer_aux_t *settings, dlib::tmp_stack_t *stack)
    {
        // This selects the proper function to use for deciding if reads belong in the same stack.
        // It chooses the single-end or paired-end based on is_se and the bmf or pos based on cmpkey.
        std::function<int (bam1_t *, bam1_t *)> fn = fns[settings->is_se | (settings->cmpkey<<1)];
        if(strcmp(dlib::get_SO(settings->hdr).c_str(), sorted_order_strings[settings->cmpkey])) {
            LOG_EXIT("Sort order (%s) is not expected %s for rescue mode. Abort!\n",
                     dlib::get_SO(settings->hdr).c_str(), sorted_order_strings[settings->cmpkey]);
        }
        bam1_t *b = bam_init1();
        if(sam_read1(settings->in, settings->hdr, b) < 0)
            LOG_EXIT("Failed to read first record in bam file. Abort!\n");
        // Zoom ahead to first primary alignment in bam.
        while(b->core.flag & (BAM_FSUPPLEMENTARY | BAM_FSECONDARY | BAM_FUNMAP | BAM_FMUNMAP)) {
            sam_write1(settings->out, settings->hdr, b);
            if(sam_read1(settings->in, settings->hdr, b) < 0)
                LOG_EXIT("Could not read first primary alignment in bam (%s). Abort!\n", settings->in->fn);
        }
        // Start stack
        stack_insert(stack, b);
        while (LIKELY(sam_read1(settings->in, settings->hdr, b) >= 0)) {
            if(b->core.flag & (BAM_FSUPPLEMENTARY | BAM_FSECONDARY | BAM_FUNMAP | BAM_FMUNMAP)) {
                sam_write1(settings->out, settings->hdr, b);
                continue;
            }
            if(fn(b, *stack->a) == 0) {
                // New stack -- flatten what we have and write it out.
                //LOG_DEBUG("Flattening stack.\n");
                flatten_stack_linear(stack, settings); // Change this later if the chemistry necessitates it.
                //LOG_DEBUG("Writing stack.\n");
                write_stack(stack, settings);
                stack->n = 1;
                stack->a[0] = bam_dup1(b);
            } else {
                // Keep adding bam records.
                stack_insert(stack, b);
            }
        }
        LOG_DEBUG("Clean up after.\n");
        flatten_stack_linear(stack, settings); // Change this later if the chemistry necessitates it.
        write_stack(stack, settings);
        stack->n = 1;
        bam_destroy1(b);
        // Handle any unpaired reads, though there shouldn't be any in real datasets.
        if(settings->realign_pairs.size()) {
            LOG_WARNING("There shoudn't be any orphaned reads left in real datasets, but there are %lu. Something may be wrong....\n", settings->realign_pairs.size());
            for(auto& pair: settings->realign_pairs) {
                fprintf(settings->fqh, pair.second.c_str());
                settings->realign_pairs.erase(pair.first);
            }
        }
    }

    int infer_usage(int retcode)
    {
        fprintf(stderr,
                        "Positional rescue. \n"
                        "Reads with the same start position (or unclipped start, if -u is set) are compared.\n"
                        "If their barcodes are sufficiently similar, they are treated as having originated"
                        "from the same original template molecule.\n"
                        "Usage:  bmftools infer <input.srt.bam> <output.bam>\n\n"
                        "Flags:\n"
                        "-f      Path for the fastq for reads that need to be realigned. REQUIRED.\n"
                        "-t      Mismatch limit. Default: 2\n"
                        "-l      Set bam compression level. Valid: 0-9. (0 == uncompresed)\n"
                        "-u      Flag to use unclipped start positions instead of pos/mpos for identifying potential duplicates.\n"
                        "Note: -u requires pre-processing with bmftools mark_unclipped.\n"
                );
        return retcode;
    }


    int infer_main(int argc, char *argv[])
    {
        int c;
        char wmode[4] = "wb";

        infer_aux_t settings = {0};
        settings.mmlim = 2;
        settings.cmpkey = POSITION;

        char *fqname = nullptr;

        if(argc < 3) return infer_usage(EXIT_FAILURE);

        while ((c = getopt(argc, argv, "l:f:t:Sau?h")) >= 0) {
            switch (c) {
            case 'u':
                settings.cmpkey = UNCLIPPED;
                LOG_INFO("Unclipped start position chosen for cmpkey.\n");
                break;
            case 't': settings.mmlim = atoi(optarg); break;
            case 'f': fqname = optarg; break;
            case 'l': wmode[2] = atoi(optarg)%10 + '0';break;
            case 'S': settings.write_supp = 1; break;
            case '?': case 'h': return infer_usage(EXIT_SUCCESS);
            }
        }
        if (optind + 2 > argc)
            return infer_usage(EXIT_FAILURE);

        if(!fqname) {
            fprintf(stderr, "Fastq path for rescued reads required. Abort!\n");
            return infer_usage(EXIT_FAILURE);
        }

        settings.fqh = fopen(fqname, "w");

        if(!settings.fqh)
            LOG_EXIT("Failed to open output fastq for writing. Abort!\n");

        if(settings.cmpkey == UNCLIPPED)
            dlib::check_bam_tag_exit(argv[optind], "MU");
        //dlib::check_bam_tag_exit(argv[optind], "LM");
        LOG_INFO("Finished checking appropriately\n");
        settings.in = sam_open(argv[optind], "r");
        settings.hdr = sam_hdr_read(settings.in);

        if (!settings.in || settings.hdr == nullptr || settings.hdr->n_targets == 0)
            LOG_EXIT("input SAM does not have header or is malformed. Abort!\n");

        settings.out = sam_open(argv[optind+1], wmode);
        if (settings.in == 0 || settings.out == 0)
            LOG_EXIT("fail to read/write input files\n");
        sam_hdr_write(settings.out, settings.hdr);

        dlib::tmp_stack_t stack = {0};
        dlib::resize_stack(&stack, STACK_START);
        if(!stack.a)
            LOG_EXIT("Failed to start array of bam1_t structs...\n");

        infer_core(&settings, &stack);

        free(stack.a);
        bam_hdr_destroy(settings.hdr);
        sam_close(settings.in); sam_close(settings.out);
        if(settings.fqh) fclose(settings.fqh);
        LOG_INFO("Successfully completed bmftools infer.\n");
        return EXIT_SUCCESS;
    }
}
