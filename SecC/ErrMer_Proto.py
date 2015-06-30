from __future__ import division
import numpy as np
from utilBMF.HTSUtils import pFastqProxy, pFastqFile, getBS, RevCmp
from itertools import groupby
from utilBMF.ErrorHandling import ThisIsMadness
from MawCluster.BCFastq import GetDescTagValue
import argparse
import operator
import pysam
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
# cimport pysam.calignmentfile
# ctypedef pysam.calignmentfile.AlignedSegment AlignedSegment_t
mcoptBS = operator.methodcaller("opt", "BS")
nucList = ["A", "T", "C", "G", "N"]


def getArgs():
    parser = argparse.ArgumentParser(description="blah")
    parser.add_argument("reads1")
    parser.add_argument("reads2")
    parser.add_argument("consBam")
    parser.add_argument("kmerLen")
    return parser.parse_args()


def HeatMaps(heatedKmers, topKmers):
    for kmer, wrong in topKmers:
        fig, ax = plt.subplots()
        plt.suptitle(kmer+"  "+str(wrong), fontsize=24)
        data = heatedKmers[kmer]
        # np.fill_diagonal(data,0)
        heatmap = ax.pcolor(data, cmap=plt.cm.Blues, edgecolors='k')
        ax.set_xticks(np.arange(data.shape[0])+0.5, minor=False)
        ax.set_yticks(np.arange(data.shape[1])+0.5, minor=False)
        ax.set_xticklabels(nucList, minor=False)
        ax.set_yticklabels(nucList, minor=False)
        plt.xlabel("Consensus Base")
        plt.ylabel("Record Base")
        for y in range(data.shape[0]):
            for x in range(data.shape[1]):
                plt.text(x+0.5, y+0.5, str(heatedKmers[kmer][y, x])[:5],
                         horizontalalignment='center',
                         verticalalignment='center',)
        plt.savefig(kmer)


def recsConsCompare(recGen, consRead, kmerDict, kmerTotals, k):
    """Compares read and consensus to build a dictionary of context specific
        errors"""
    nucIndex = {"A": 0, "C": 1, "T": 2, "G": 3}
    for rec in recGen:
        recSeq = rec.sequence
        if consRead.is_reverse:
            conSeq = RevCmp(consRead.query_sequence)
        else:
            conSeq = consRead.query_sequence
        if(recSeq == conSeq):
            continue
        for baseIndex in xrange(len(rec.sequence)-k):
            recKmer = recSeq[baseIndex:baseIndex+k]
            consKmer = conSeq[baseIndex:baseIndex+k]
            if(recKmer != consKmer):
                continue
            recBase = recSeq[baseIndex+k]
            consBase = conSeq[baseIndex+k]
            if recBase == "N":
                continue
            try:
                kmerDict[recKmer]
                pass
            except KeyError:
                kmerDict[recKmer] = np.zeros((4, 4), dtype=np.float)
                kmerTotals[recKmer] = [0, 0]
            kmerDict[recKmer][nucIndex[consBase]][nucIndex[recBase]] += 1.0
            kmerTotals[recKmer][1] += 1
            if recBase != consBase:
                kmerTotals[recKmer][0] += 1


def ParseMDZ(mdzStr):
    """
    Returns the base position of mismatches between the current read and
    the reference, given the MD Z string and the start position for the read.
    """
    mismatches = []
    lastItem = None
    oper = []
    counts = [str(i) for i in range(0, 10)]+["^"]
    for item in mdzStr:
        curval = item
        if item in nucList:
            itemType = "base"
        if item in counts:
            itemType = "count"
        if itemType == lastItem:
            oper.append(item)
            continue
        else:
            oper = [item]


def GetRefKmers(refSeq):
    pass


def main():
    args = getArgs()
    readsInFq1 = pFastqFile(args.reads1)
    readsInFq2 = pFastqFile(args.reads2)
    bamGen = groupby(pysam.AlignmentFile(args.consBam), key=mcoptBS)
    k = int(args.kmerLen)
    bgn = bamGen.next
    r2GenGen = groupby(readsInFq2, key=getBS)
    r2ggn = r2GenGen.next
    kmerDict = {}
    kmerTotals = {}
    qualDict = {}
    #refKmers = getRefKmers(args.Ref)
    for bc4fq1, fqRecGen1, in groupby(readsInFq1, key=getBS):
        consBS, consReads = bgn()
        bc4fq2, fqRecGen2 = r2ggn()
        consRead1 = None
        consRead2 = None
        if(bc4fq1 != bc4fq2):
            raise ThisIsMadness("Fastq barcodes don't match")
        if(consBS != bc4fq1):
            raise ThisIsMadness("Bam Barcode mismatch")
        for cRead in consReads:
            if cRead.is_secondary:
                continue
            if cRead.is_supplementary:
                continue
            if cRead.is_read1:
                consRead1 = cRead
            if cRead.is_read2:
                consRead2 = cRead
        if consRead1 is None or consRead2 is None:
            print "Did not find both reads in bam"
        if consRead1.opt("FM") <= 100:
            continue
        recsConsCompare(fqRecGen1, consRead1, kmerDict, kmerTotals, k)
        recsConsCompare(fqRecGen2, consRead2, kmerDict, kmerTotals, k)
    wrongKmers = {kmer: kmerTotals[kmer][0]/kmerTotals[kmer][1] for kmer
                  in kmerTotals}
    sortedKmers = sorted(wrongKmers.items(), key=operator.itemgetter(1),
                         reverse=True)
    heatedKmers = {kmer: kmerDict[kmer]/kmerDict[kmer].sum(axis=0) for kmer in
                   kmerDict}
    HeatMaps(heatedKmers, sortedKmers[:10])


if __name__ == "__main__":
    main()