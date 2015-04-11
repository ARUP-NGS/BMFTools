# cython: c_string_type=str, c_string_encoding=ascii
# cython: profile=True, cdivision=True, cdivision_warnings=True

import logging
import operator
import math
from math import log10 as mlog10
from operator import methodcaller as mc
import uuid
import shlex
import subprocess

import cython
cimport cython
import numpy as np
cimport numpy as np
import pysam
cimport pysam.TabProxies

from MawCluster.BCVCF import IterativeVCFFile
from utilBMF.HTSUtils import printlog as pl, ThisIsMadness, NameSortAndFixMate
from utilBMF.ErrorHandling import IllegalArgumentError
from MawCluster.SNVUtils import HeaderFilterDict, HeaderFunctionCallLine
from MawCluster.Probability import ConfidenceIntervalAAF, GetCeiling
from MawCluster.BCVCF import IterativeVCFFile

ctypedef np.longdouble_t dtype128_t


"""
Contains utilities relating to FFPE
"""

BMFVersion = "0.0.7.3"


@cython.locals(pVal=dtype128_t, DOC=cython.long,
               maxFreqNoise=dtype128_t, ctfreq=dtype128_t, AAF=dtype128_t,
               recordsPerWrite=cython.long)
def FilterByDeaminationFreq(inVCF, pVal=0.001, ctfreq=0.018,
                            recordsPerWrite=5000, outVCF="default"):
    """
    If observed AAF is greater than the upper limit of the confidence window
    with a given P-Value, the variant is permitted to stay.
    Otherwise, DeaminationNoise replaces PASS or is appended to other filters.
    """
    pl("C-T/G-A frequency set to %s" % ctfreq)
    IVCFObj = IterativeVCFFile(inVCF)
    if(outVCF == "default"):
        outVCF = ".".join(inVCF.split(".")[0:-1] + ["ctfilt", "vcf"])
    pl("FilterByDeaminationFreq called. inVCF: %s. outVCF: %s." % (inVCF,
                                                                   outVCF))
    outHandle = open(outVCF, "w")
    mfdnpStr = str(int(-10 * mlog10(pVal)))
    functionCall = ("FilterByDeaminationFreq(%s, pVal=%s, " % (inVCF, pVal) +
                    "ctfreq=%s, recordsPerWrite=" % ctfreq +
                    "%s). BMFTools version: %s" % (recordsPerWrite,
                                                   BMFVersion))
    IVCFObj.header.insert(-1, HeaderFunctionCallLine(functionCall).__str__())
    outHandle.write("\n".join(IVCFObj.header) + "\n")
    recordsArray = []
    for line in IVCFObj:
        if(len(recordsArray) >= recordsPerWrite):
            strArray = map(str, recordsArray)
            zipped = zip(recordsArray, strArray)
            for pair in zipped:
                if(pair[1] == ""):
                    pl("Empty string for VCF record %s" % repr(pair[0]) +
                        "%s:%s:%s:%s" % (pair[0].CHROM, pair[0].POS,
                                         pair[0].REF, pair[0].ALT),
                       level=logging.DEBUG)
            outHandle.write("\n".join(map(str, recordsArray)) +
                            "\n")
            recordsArray = []
        if(line.REF != "C" or line.REF != "G"):
            recordsArray.append(line)
            continue
        if(line.REF == "C" and line.ALT != "T"):
            recordsArray.append(line)
            continue
        if(line.REF == "G" and line.ALT != "A"):
            recordsArray.append(line)
            continue
        DOC = int(line.GenotypeDict["DP"])
        AAF = float(line.InfoDict["AC"]) / DOC
        maxFreqNoise = GetCeiling(DOC, p=ctfreq, pVal=pVal) / (DOC * 1.)
        if AAF < maxFreqNoise:
            if(line.FILTER == "PASS"):
                line.FILTER = "DeaminationNoise"
            else:
                line.FILTER += ",DeaminationNoise"
        line.InfoDict["MFDN"] = maxFreqNoise
        line.InfoDict["MFDNP"] = mfdnpStr
        recordsArray.append(line)
    outHandle.write("\n".join(map(str, recordsArray)) + "\n")
    outHandle.flush()
    outHandle.close()
    return outVCF


@cython.locals(maxFreq=dtype128_t)
@cython.returns(dtype128_t)
def GetDeaminationFrequencies(inVCF, maxFreq=0.15, FILTER="",
                              minGCcount=50):

    """
    Returns a list of raw base frequencies for G->A and C->T.
    Only accepts SNVCrawler's VCF output as it requires those INFO fields.
    FILTER must be a comma-separated list of FILTER strings as defined
    in the VCF header.
    This is slower than the Py version, so I will be working with it to
    get some speed soon.
    """
    cdef cython.long TotalCG
    cdef cython.long TotalCG_TA
    cdef cython.long DP
    cdef cython.long GC
    cdef dtype128_t freq
    validFilters = map(mc("lower"), HeaderFilterDict.keys())
    FILTER = FILTER.lower()
    filters = FILTER.split(",")
    pl("Filters: %s" % filters)
    pl("maxFreq: %s" % maxFreq)
    if(FILTER != ""):
        for filter in filters:
            if filter not in validFilters:
                raise IllegalArgumentError("Filter must be a valid VCF Filter. "
                                           "%s" % validFilters)
    IVCFObj = IterativeVCFFile(inVCF)
    TotalCG = 0
    TotalCG_TA = 0
    for line in IVCFObj:
        if(FILTER != ""):
            for filter in filters:
                if filter in line.FILTER.lower():
                    pl("Failing line for filter %s" % filter,
                       level=logging.DEBUG)
                    continue
        lid = line.InfoDict
        cons = lid["CONS"]
        MACSStr = lid["MACS"]
        macsDict = dict(map(mc(
            "split", ">"), MACSStr.split(",")))
        if(cons == "C" and line.REF == "C"):
            GC = int(macsDict["C"])
            TA = int(macsDict["T"])
            if(GC < minGCcount):
                continue
            if(GC == 0):
                continue
            if(TA * 1.0 / GC <= maxFreq):
                TotalCG_TA += TA
                TotalCG += GC
        elif(cons == "G" or line.REF == "G"):
            GC = int(macsDict["G"])
            if(GC < minGCcount):
                continue
            TA = int(macsDict["A"])
            if(GC == 0):
                continue
            if(TA * 1.0 / GC <= maxFreq):
                TotalCG_TA += TA
                TotalCG += GC
    freq = 1. * TotalCG_TA / TotalCG
    pl("TotalCG_TA: %s. TotalCG: %s." % (TotalCG_TA, TotalCG))
    pl("Estimated frequency: %s" % freq)
    pl("For perspective, a 0.001 pValue ceiling at 100 DOC would be %s" % (GetCeiling(
        100, p=freq, pVal=0.001) / 100.))
    pl("Whereas, a 0.001 pValue ceiling at 1000 DOC would be %s" % (GetCeiling(
        1000, p=freq, pVal=0.001) / 1000.))
    return freq


def PyGetDeamFreq(inVCF, maxFreq=0.15, FILTER="",
                  minGCcount=50):

    """
    Trying to see if the C compilation is throwing anything off.
    Returns a list of raw base frequencies for G->A and C->T.
    Only accepts SNVCrawler's VCF output as it requires those INFO fields.
    FILTER must be a comma-separated list of FILTER strings as defined
    in the VCF header.
    """
    validFilters = map(mc("lower"), HeaderFilterDict.keys())
    filters = FILTER.lower().split(",")
    pl("Filters: %s" % filters)
    pl("maxFreq: %s" % maxFreq)
    if(FILTER != ""):
        for filter in filters:
            if filter not in validFilters:
                raise IllegalArgumentError("Filter must be a valid VCF Filter. "
                                           "%s" % validFilters)
    IVCFObj = IterativeVCFFile(inVCF)
    TotalCG = 0
    TotalCG_TA = 0
    for line in IVCFObj:
        if(FILTER != ""):
            for filter in filters:
                if filter in line.FILTER.lower():
                    pl("Failing line for filter %s" % filter,
                       level=logging.DEBUG)
                    continue
        lid = line.InfoDict
        cons = lid["CONS"]
        MACSStr = lid["MACS"]
        macsDict = dict(map(mc(
            "split", ">"), MACSStr.split(",")))
        if((cons == "C" or line.REF == "C") and line.ALT == "C"):
            GC = int(macsDict["C"])
            TA = int(macsDict["T"])
            if(GC < minGCcount):
                continue
            if(GC == 0):
                continue
            if(TA * 1.0 / GC <= maxFreq):
                TotalCG_TA += TA
                TotalCG += GC
        elif((cons == "G" or line.REF == "G") and line.ALT == "A"):
            GC = int(macsDict["G"])
            if(GC < minGCcount):
                continue
            TA = int(macsDict["A"])
            if(GC == 0):
                continue
            if(TA * 1.0 / GC <= maxFreq):
                TotalCG_TA += TA
                TotalCG += GC
    freq = 1. * TotalCG_TA / TotalCG
    pl("TotalCG_TA: %s. TotalCG: %s." % (TotalCG_TA, TotalCG))
    pl("Estimated frequency: %s" % freq)
    pl("For perspective, a 0.001 pValue ceiling at 100 DOC would be %s" % (GetCeiling(
        100, p=freq, pVal=0.001) / 100.))
    pl("Whereas, a 0.001 pValue ceiling at 1000 DOC would be %s" % (GetCeiling(
        1000, p=freq, pVal=0.001) / 1000.))
    return freq



@cython.locals(maxFreq=dtype128_t, pVal=dtype128_t)
def TrainAndFilter(inVCF, maxFreq=0.1, FILTER="",
                   pVal=0.001):
    """
    Calls both GetDeaminationFrequencies and FilterByDeaminationFreq.
    """
    cdef dtype128_t DeamFreq
    DeamFreq = GetDeaminationFrequencies(inVCF, maxFreq=maxFreq,
                                         FILTER=FILTER)
    pl("Estimated deamination frequency: %s" % DeamFreq)
    OutVCF = FilterByDeaminationFreq(inVCF, pVal=pVal,
                                     ctfreq=DeamFreq)
    pl("Output VCF: %s" %OutVCF)
    return OutVCF


@cython.locals(primerLen=cython.long, fixmate=cython.bint)
def PrefilterAmpliconSequencing(inBAM, primerLen=20, outBAM="default",
                                fixmate=True):
    """
    This program outputs a BAM file which eliminates potential mispriming
    events from the input BAM file.
    """
    if(outBAM == "default"):
        outBAM = ".".join(inBAM.split(".")[0:-1] + ["amplicon",
                                                    "filt", "bam"])
    pl("Primer length set to %s for prefiltering." % primerLen )
    pl("OutBAM: %s" % outBAM)
    pl("fixmate: %s" % fixmate)
    tempFile = str(uuid.uuid4().get_hex().upper()[0:8]) + ".bam"
    inHandle = pysam.AlignmentFile(inBAM, "rb")
    outHandle = pysam.AlignmentFile(tempFile, "wb", template=inHandle)
    for rec in inHandle:
        tempQual = rec.qual[primerLen:]
        rec.seq = rec.seq[primerLen:]
        rec.qual = tempQual
        if(rec.is_reverse):
            rec.pos -= primerLen
        else:
            rec.pos += primerLen
        outHandle.write(rec)
    inHandle.close()
    outHandle.close()
    if(fixmate):
        newTemp = NameSortAndFixMate(tempFile, sortAndIndex=True)
        subprocess.check_call(["mv", newTemp, outBAM])
    return outBAM


@cython.locals(rec=pysam.TabProxies.VCFProxy)
def makeinfodict(rec):
    """
    Returns a dictionary of info fields for a tabix VCF Proxy
    """
    return dict([i.split("=") for i in rec.info.split(";")])


@cython.returns(cython.float)
def getFreq(pysam.TabProxies.VCFProxy rec, l="d"):
    """
    Returns allele frequency for a tabix VCF Proxy made from SNVCrawler.
    """
    return float(dict([i.split(">") for i in
                       makeinfodict(rec)["MAFS"].split(",")])[l])


@cython.returns(cython.float)
def GetTabixDeamFreq(inVCF):
    """
    Gets deamination frequency for a tabixed VCF file, under the assumption
    that the majority of C-T/G-A calls at low frequencies which are not
    ablated by family demultiplexing are due to formalin fixation.
    """
    cdef cython.long atCounts
    cdef cython.long gcCounts
    cdef pysam.TabProxies.VCFProxy rec
    cdef cython.float freq
    atCounts = 0
    gcCounts = 0
    import sys
    import pysam
    a = pysam.tabix_iterator(open(inVCF, "rb"), pysam.asVCF())
    for rec in a:
        mid = makeinfodict(rec)
        if(mid["CONS"] == "C" and getFreq(rec, "T") / getFreq(rec, "C") < 0.15 and rec.alt == "T"):
            atCounts += int(dict([i.split(">") for i in mid["MACS"].split(",")])["T"])
            gcCounts += int(dict([i.split(">") for i in mid["MACS"].split(",")])["C"])
        if(mid["CONS"] == "G" and getFreq(rec, "A") / getFreq(rec, "G") < 0.15  and rec.alt == "A"):
            atCounts += int(dict([i.split(">") for i in mid["MACS"].split(",")])["A"])
            gcCounts += int(dict([i.split(">") for i in mid["MACS"].split(",")])["G"])
        if(rec.ref == "C" and getFreq(rec, "T") < 0.25 and
           getFreq(rec, "C") >= 0.3 and rec.alt == "T"):
            atCounts += int(dict([i.split(">") for i in mid["MACS"].split(",")])["T"])
            gcCounts += int(dict([i.split(">") for i in mid["MACS"].split(",")])["C"])
        if(rec.ref == "G" and getFreq(rec, "A") < 0.25 and
           getFreq(rec, "G") >= 0.3 and rec.alt == "A"):
            atCounts += int(dict([i.split(">") for i in mid["MACS"].split(",")])["A"])
            gcCounts += int(dict([i.split(">") for i in mid["MACS"].split(",")])["G"])
    freq = (1. * atCounts) / gcCounts
    print("Final atCounts: %s" % atCounts)
    print("Final gcCounts: %s" % gcCounts)
    print("Est deam freq: %s" % (freq))
    return freq

