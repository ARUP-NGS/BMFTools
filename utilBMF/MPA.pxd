cimport cython
cimport numpy as np
cimport pysam.calignmentfile
cimport pysam.cfaidx

cimport utilBMF.HTSUtils

from cpython.array cimport array as py_array
from numpy cimport ndarray
from cython cimport bint
from utilBMF.Inliners cimport (ph2chrInline, CigarOpToCigarChar, ChrToRefIDInline,
                               chr2phInline, chrInline, opLenToStr)
from utilBMF.PysamUtils cimport PysamToChrInline

ctypedef cython.str cystr
ctypedef pysam.calignmentfile.AlignedSegment AlignedSegment_t

ctypedef PyLayout PyLayout_t
ctypedef LayoutPos LayoutPos_t
ctypedef utilBMF.HTSUtils.BamTag BamTag_t

cpdef MPA2stdout(cystr inBAM)

cdef class LayoutPos:
    cdef public cython.int pos, readPos, quality, agreement
    cdef public char operation, base, mergeAgreed
    cdef bint merged
    # cdef public cystr
    cpdef bint ismapped(self)
    cdef bint getMergeAgreed(self)
    cdef bint getMergeSet(self)


cdef class PyLayout:
    cdef public list positions
    cdef public dict tagDict
    cdef public cython.int firstMapped, InitPos, flag, pnext, tlen, mapq
    cdef public cystr Name, contig, rnext
    cdef public bint isMerged, is_reverse, mergeAdjusted
    cdef int aend

    cpdef cython.int getAlignmentStart(self)
    cpdef cystr getCigarString(self)
    cpdef cystr getSeq(self)
    cdef py_array getSeqArr(self)
    cdef int cGetRefPosForFirstPos(self)
    cpdef int getRefPosForFirstPos(self)
    cpdef py_array getAgreement(self)
    cdef py_array cGetAgreement(self)
    cdef py_array cGetQual(self)
    cpdef py_array getQual(self)
    cdef cystr cGetQualString(self)
    cpdef cystr getQualString(self)
    cdef int cGetLastRefPos(self)
    cpdef int getLastRefPos(self)
    cdef cystr cGetCigarString(self)
    cdef update_tags(self)
    cdef py_array cGetMergedPositions(self)
    cdef py_array cGetMergeAgreements(self)
    cpdef py_array getMergedPositions(self)
    cpdef py_array getMergeAgreements(self)
    cdef py_array cGetGenomicDiscordantPositions(self)
    cdef py_array cGetReadDiscordantPositions(self)

cpdef bint LayoutsOverlap(PyLayout_t L1, PyLayout_t L2)
cdef LayoutPos_t cMergePositions(LayoutPos_t pos1, LayoutPos_t pos2)
cdef int getLayoutLen(AlignedSegment_t read)

cdef class ListBool:
    cdef list List
    cdef bint Bool
