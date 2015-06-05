cimport cython
cimport pysam.calignmentfile
cimport pysam.cfaidx
cimport numpy as np
from numpy cimport ndarray
ctypedef PileupReadPair PileupReadPair_t
ctypedef np.longdouble_t dtype128_t
ctypedef pPileupRead pPileupRead_t
ctypedef ReadPair ReadPair_t
ctypedef pysam.calignmentfile.AlignedSegment cAlignedSegment

cdef class pPileupRead:
    """
    Python container for the PileupRead proxy in pysam
    """
    cdef public cython.str BaseCall
    cdef public cython.bint is_del
    cdef public long level
    cdef public long indel
    cdef public long query_position
    cdef public cython.str name, str
    cdef public cAlignedSegment alignment
    cpdef object opt(self, cython.str arg)

cdef class PileupReadPair:

    """
    Holds both bam record objects in a pair of pileup reads.
    Currently, one read unmapped and one read soft-clipped are
    both marked as soft-clipped reads.
    Accepts a list of length two as input.
    """

    cdef public pPileupRead_t read1
    cdef public pPileupRead_t read2
    cdef public ReadPair_t RP
    cdef public cython.bint discordant
    cdef public cython.str discordanceString
    cdef public cython.str name


cdef class ReadPair:

    """
    Holds both bam record objects in a pair.
    Currently, one read unmapped and one read soft-clipped are
    both marked as soft-clipped reads.
    """
    cdef public cAlignedSegment read1
    cdef public cAlignedSegment read2
    cdef public list SVTags
    cdef public cython.bint read1_is_unmapped
    cdef public cython.bint read1_soft_clipped
    cdef public cython.bint read2_is_unmapped
    cdef public cython.bint read2_soft_clipped
    cdef public cython.bint SameContig
    cdef public cython.str read1_contig
    cdef public cython.str read2_contig
    cdef public cython.str ContigString
    cdef public long insert_size
    cdef public cython.bint read1_in_bed
    cdef public cython.bint read2_in_bed, SameStrand


cdef class AbstractIndelContainer:
    """
    Base class for insertion and deletion container objects.

    Type can be -1, 0, or 1. (Deletion, deletion and insertion, and just insertion)
    Start and end refer to different things for insertions and deletions.
    For a deletion, start is the first missing base and end is the last missing
    reference base position.
    seq should be None for a deletion
    """
    cdef public cython.str contig, seq, uniqStr
    cdef public long type, shenwindow, end, start
    cdef public cython.float shen
    cdef public list readnames, StartStops
    cdef public pysam.cfaidx.FastaFile handle

cdef class Deletion(AbstractIndelContainer):
    """
    See HTSUtils.pyx for doc string.
    """


cdef class Insertion(AbstractIndelContainer):
    """
    See HTSUtils.pyx for doc string.
    """

cdef class IndelQuiver(object):
    cdef public dict data, counts, readnames
    cdef public long window, minMQ, minFM, minPairs, minNumSS
    cdef public pysam.cfaidx.FastaFile fastaRef
    cdef public pysam.calignmentfile.AlignmentFile bam
    cdef public cython.float minShen


cdef class IDVCFLine(object):

    """
    Contains data for writing to a VCF Line, where each ALT has its own line.

    minNumSS is the minumum number of start/stop combinations required to
    support a variant call.
    """

    cdef public cython.str TYPE, REF, ALT, ID, CHROM, FILTER, FormatStr, str
    cdef public long POS, LEN, NumStartStops, NDPS, DPA
    cdef public cython.float reverseStrandFraction, QUAL, MDP
    cdef public cython.bint BothStrandSupport
    cdef public dict InfoFields, FormatFields

cdef class pFastqProxy:
    """
    Python container for pysam.cfaidx.FastqProxy with persistence.
    """
    cdef public cython.str comment, quality, sequence, name
    cdef cython.str getBS_(self)
    cpdef cython.str getBS(self)
    cdef cython.str tostring(self)

cdef class BamTag(object):
    """
    Contains a tag, a value, and a type, all of which are string objects.
    """
    cdef readonly cython.str tag
    cdef readonly cython.str tagtype
    cdef public object value

cdef class pFastqFile(object):
    cdef public pysam.cfaidx.FastqFile handle
    cpdef close(self)

cpdef public cython.str RevCmp(cython.str seq, dict CmpDict=?)

cpdef public list permuteNucleotides(long maxn, object nci=?)

cpdef cython.bint ReadsOverlap(
        cAlignedSegment read1,
        cAlignedSegment read2)


cdef cython.bint cReadsOverlap(
        cAlignedSegment read1,
        cAlignedSegment read2)

cpdef cython.bint WritePairToHandle(ReadPair_t pair, pysam.calignmentfile.AlignmentFile handle=?)
cdef double cyOptStdDev_(ndarray[np.float64_t, ndim=1] a)