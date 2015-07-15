cimport cython
cimport numpy as np
cimport pysam.calignmentfile
cimport pysam.cfaidx
cimport utilBMF.HTSUtils
from cpython cimport array as c_array
from libc.math cimport log10 as c_log10
from libc.stdlib cimport malloc, free, realloc, calloc
from libc.stdint cimport int8_t, int32_t
from libc.string cimport memcpy
from numpy cimport ndarray, uint8_t
from utilBMF.cstring cimport cs_to_ph, cs_to_ia, PH2CHR_TRANS
from utilBMF.HTSUtils cimport chr2ph, chr2phStr, int2Str, ph2chrDict
from utilBMF.Inliners cimport Num2Nuc, Nuc2Num
from MawCluster.Math cimport igamc
ctypedef c_array.array py_array
ctypedef cython.str cystr
ctypedef SeqQual SeqQual_t
ctypedef utilBMF.HTSUtils.pFastqFile pFastqFile_t
ctypedef utilBMF.HTSUtils.pFastqProxy pFastqProxy_t


# CONSTANTS
cdef public dict Num2NucDict
cdef public cystr ARGMAX_TRANSLATE_STRING
cdef public py_array nucs
cdef char size32 = 4
cdef char sizedouble = 8
cdef double LOG10E = 0.43429448190325182765
cdef double LOG10E_X5 = 2.1714724095162592
cdef double LOG10E_X5_INV = 0.46051701859880917  # Multiply a phred score by this to convert

# METHODS
cdef cystr cCompareFqRecsFast(list R, cystr name=?, double minPVFrac=?,
                              double minFAFrac=?, double minMaxFA=?)
cpdef cystr pCompareFqRecsFast(list R, cystr name=?)
cpdef cystr MakeTagComment(cystr saltedBS, pFastqProxy_t rec, int)

cdef cystr cQualArr2QualStr(ndarray[int32_t, ndim=1] qualArr)
cpdef cystr QualArr2QualStr(ndarray[int32_t, ndim=1] qualArr)

cpdef cystr QualArr2PVString(ndarray[int32_t, ndim=1] qualArr)
cdef cystr cQualArr2PVString(ndarray[int32_t, ndim=1] qualArr)
cdef cystr cQualArr2FAString(ndarray[int32_t, ndim=1] qualArr)


cdef inline bint BarcodePasses(cystr barcode, int hpLimit)


@cython.boundscheck(False)
@cython.initializedcheck(False)
@cython.wraparound(False)
cdef inline cystr cMakeTagComment(cystr saltedBS,
                                  pFastqProxy_t rec, int hpLimit):
    cdef bint PASS = BarcodePasses(saltedBS, hpLimit)
    if(PASS):
        return "~#!#~" + rec.comment + "|FP=1|BS=" + saltedBS
    else:
        return "~#!#~" + rec.comment + "|FP=0|BS=" + saltedBS

@cython.boundscheck(False)
@cython.initializedcheck(False)
@cython.wraparound(False)
cdef inline double CHI2_FROM_PHRED(int32_t phredInt) nogil:
    return phredInt * LOG10E_X5_INV


@cython.boundscheck(False)
@cython.initializedcheck(False)
@cython.wraparound(False)
cdef inline np.double_t c_max(np.double_t a, np.double_t b) nogil:
    return a if(a > b) else b


@cython.boundscheck(False)
@cython.initializedcheck(False)
@cython.wraparound(False)
cdef inline int8_t argmax4(np.double_t a, np.double_t c, np.double_t g,
                           np.double_t t) nogil:
    if t > c and t > g and  t > a:
        return 3
    elif g > c and g > a:
        return 2
    elif c > a:
        return 1
    else:
        return 0


@cython.boundscheck(False)
@cython.initializedcheck(False)
@cython.wraparound(False)
cdef inline int8_t ARGMAX_CONV(int8_t index) nogil:
    if(index == 1):
        return 67
    elif(index == 2):
        return 71
    elif(index == 3):
        return 84
    else:
        return 67

@cython.boundscheck(False)
@cython.initializedcheck(False)
@cython.wraparound(False)
cdef inline double igamc_pvalues(int num_pvalues, double x) nogil:
    return 1.0 if(x < 0) else igamc(num_pvalues * 1., x / 2.0)

# STRUCTS
cdef struct SeqQual:
    int8_t * Seq
    int32_t * Agree
    int32_t * Qual
