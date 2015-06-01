"""
Contains a set of utilities for evaluating uniqueness of both
mappability and capture. Also might have applications in assembly-based
variant-calling.
"""
import cython
import pysam
import uuid
import logging
from subprocess import check_output, check_call
from utilBMF.HTSUtils import (GetUniqueItemsL, GetUniqueItemsD,
                              ThisIsMadness, printlog as pl, ParseBed)
from cytoolz import frequencies as cyfreq
cimport cython


@cython.returns(cython.str)
def SequenceToFakeFq(cython.str seq):
    return ("@" + seq + "\n" + seq +
            "\n+\n" + "G" * len(seq))


@cython.returns(list)
def GetKmersToCheck(cython.str ref, cython.int k=30, list bedline=[],
                    cython.int padding=-1):
    """
    Gets a list of kmers which provide unique mappability
    to the region of interest.
    bedline should be the first 3 columns from a line in a bed file.
    """
    cdef cython.int i, start, end
    cdef list kmerList, candidateKmers
    if(padding < 0):
        pl("Padding not set - defaults to kmer size.")
        padding = k
    kmerList = []
    refHandle = pysam.FastaFile(ref)
    contig, start = bedline[0], bedline[1] - padding
    end = bedline[2] + padding
    regionStr = refHandle.fetch(contig, start, end)
    return [regionStr[i:i + k] for i in xrange(end - start - k)]


@cython.returns(cython.str)
def FastqStrFromKmerList(list kmerList):
    """
    Creates a dummy fastq string from a list of kmers.
    """
    return "\n".join(map(SequenceToFakeFq, kmerList))


cdef class RefKmer(object):
    """
    Contains useful information regarding representative kmers selected
    from reference sequence.
    """

    def __init__(self, cython.str seq, cython.str contig=None,
                 cython.int pos=-1):
        assert pos >= 0  # pos needs to be set
        self.contig = contig
        self.seq = seq
        self.len = len(seq)
        self.pos = pos

    @cython.returns(cython.str)
    def __str__(self):
        return "%s|%s|%s" % (self.contig, self.pos, self.seq)


cdef class KmerFetcher(object):
    """
    Contains parameters for finding representative kmers.
    I want to permit the mixing of kmers of different sizes, so I have
    set a baseK, which is the K that we start using.

    minMQ defaults to 1, meaning a unique mapping for most aligners.
    Bowtie instead uses 255 to refer to unique alignments and (0,3) to mark
    multiple acceptable alignments.

    TODO: I'd like to have it know where dropout regions for these kmers
    are and have it increase the size of k only as necessary... Not sure
    how to automate that.
    """
    def __init__(self, cython.str ref=None, cython.int padding=-1,
                 cython.int mismatches=-1, cython.int minMQ=1,
                 cython.int k=30):
        self.ref = ref
        self.mismatches = mismatches
        self.minMQ = minMQ
        self.padding = padding
        self.k = k
        self.HashMap = {}

    cdef setK(self, cython.int newK):
        self.k = newK

    cdef cython.int getK(self):
        return self.k

    cpdef cython.str getFastqString(self, list bedline):
        return FastqStrFromKmerList(GetKmersToCheck(self.ref, k=self.k,
                                                    bedline=bedline,
                                                    padding=self.padding))

    cpdef cython.str getOutputString(self, list bedline):
        return BwaFqToStr(self.getFastqString(bedline), ref=self.ref)

    cpdef FillMap(self, list bedline):
        self.HashMap[
            ":".join(map(str, bedline))] = self.GetUniqueKmers(bedline)

    cpdef list GetUniqueKmers(self, list bedline):
        return GetMQPassRefKmersMem(self.getOutputString(bedline),
                                    maxNM=self.mismatches)

    cpdef FMfrombed(self, cython.str bedfile):
        cdef list bedline
        for bedline in ParseBed(bedfile):
            self.FillMap(bedline)

    def __getitem__(self, key):
        return self.HashMap[key]

    def iteritems(self, *args, **kwargs):
        return self.HashMap.iteritems(*args, **kwargs)

    def items(self, *args, **kwargs):
        return self.HashMap.items(*args, **kwargs)

    def itervalues(self, *args, **kwargs):
        return self.HashMap.itervalues(*args, **kwargs)

    def values(self, *args, **kwargs):
        return self.HashMap.values(*args, **kwargs)

    def iterkeys(self, *args, **kwargs):
        return self.HashMap.iterkeys(*args, **kwargs)

    def keys(self, *args, **kwargs):
        return self.HashMap.keys(*args, **kwargs)


@cython.returns(dict)
def GetRepresentativeKmerDict(*args, **kwargs):
    return cyfreq(GetRepKmersBwt(*args, **kwargs))


@cython.returns(list)
def GetRepKmersBwt(cython.str ref, cython.int k=30,
                   list bedline=[],
                   cython.int padding=-1, cython.int seedlen=-1,
                   cython.int mismatches=-1, cython.int minMQ=1,
                   cython.bint useBowtie=False):
    cdef cython.str fqStr, output
    fqStr = FastqStrFromKmerList(GetKmersToCheck(ref, k=k, bedline=bedline,
                                                 padding=padding))
    if(useBowtie):
        output = BowtieFqToStr(fqStr, ref=ref, seed=seedlen,
                               mismatches=mismatches)
        return GetUniqMQsBowtie(output, minMQ=minMQ)
    else:
        output = BwaFqToStr(fqStr, ref=ref, )
    return GetUniqMQsBowtie(output, minMQ=minMQ)


@cython.returns(cython.str)
def BowtieFqToStr(cython.str fqStr, cython.str ref=None,
                  cython.int seed=-1, cython.int mismatches=-1):
    """
    Returns the string output of a bowtie2 call.
    With bowtie, you can specify precisely the number of permitted mismatches
    in the string, but with bowtie2, you don't need to write any temporary
    files. I'll play around with the alignment settings as I get along in
    the getkmers tool.
    """
    if(seed < 0):
        raise ThisIsMadness("seed length must be set for Bowtie2FqToStr.")
    if(mismatches > 2 or mismatches < 0):
        raise ThisIsMadness("0 <= mismatches <= 2!")
    tmpFile = str(uuid.uuid4().get_hex()[0:8]) + ".hey.i.am.a.prefix.fq"
    tmpFileHandle = open(tmpFile, "w")
    tmpFileHandle.write(fqStr)
    tmpFileHandle.close()
    cStr = "bowtie --mm --all -n %s -l %s %s -S %s" % (mismatches, seed,
                                                       ref, tmpFile)
    pl("Bowtie command string: %s" % cStr, level=logging.DEBUG)
    print("Bowtie command string: %s" % cStr)
    outStr = check_output(cStr, shell=True)  # Capture output to string
    check_call(["rm", tmpFile])  # Delete the temporary file.
    print("Returning bowtieFqToStr output")
    return outStr


@cython.returns(cython.str)
def BwaFqToStr(cython.str fqStr, cython.str ref=None,
               cython.int seed=-1):
    """
    Returns the string output of a bwa mem call.
    """
    cdef cython.str seedStr, cStr, outStr, tmpFile
    tmpFile = str(uuid.uuid4().get_hex()[0:8]) + ".hey.i.am.a.prefix.fq"
    tmpFileHandle = open(tmpFile, "w")
    tmpFileHandle.write(fqStr)
    tmpFileHandle.close()
    if(seed > 0):
        seedStr = "-k %s" % seed
    else:
        seedStr = ""
    cStr = "bwa mem -a %s %s %s" % (ref, seedStr, tmpFile)
    pl("Bowtie command string: %s" % cStr, level=logging.DEBUG)
    print("Bwa command string: %s" % cStr)
    outStr = check_output(cStr, shell=True)  # Capture output to string
    # check_call(["rm", tmpFile])  # Delete the temporary file.
    print("Returning BwaFqToStr output")
    return outStr


@cython.returns(cython.bint)
def PassesNM(cython.str rStr, cython.int maxNM=2):
    """
    Checks a SAM line to see if its edit distance is below the minimum.
    """
    cdef cython.list strList
    cdef cython.str qStr
    strList = ["NM:i:%s\t" % i for i in range(maxNM + 1)]
    for qStr in strList:
        if(qStr in rStr):
            return True
    return False


@cython.returns(list)
def GetMQPassRefKmersMem(cython.str bwaStr, cython.int maxNM=2):
    """
    Takes a string output from bowtie and gets the names of the reads
    with MQ >= minMQ. Defaults to 1 (for a unique alignment)
    """
    cdef list lines, i
    cdef cython.str f
    cdef tuple nameCount
    return [RefKmer(i[0], contig=i[2],
                    pos=int(i[3])) for i in [f.strip().split("\t") for
                                             f in bwaStr.split("\n") if
                                             "XA:Z:" not in f and  # Supp aln
                                             f != "" and  # Empty line
                                             f[0] != "@" and  # Header
                                             PassesNM(f, maxNM=maxNM)]  # NM
            if i[4] != "0"]


@cython.returns(list)
def GetMQPassReadsMem(cython.str bwaStr):
    """
    Takes a string output from bowtie and gets the names of the reads
    with MQ >= minMQ. Defaults to 1 (for a unique alignment)
    """
    cdef list lines, i
    cdef cython.str f
    cdef tuple nameCount
    return [i[0] for i in [f.strip().split("\t") for
                           f in bwaStr.split("\n") if
                           "XA:Z:" not in f and f[0] != "@" and f != ""]
            if i[4] != "0"]


@cython.returns(list)
def GetMQPassRefKmersBwt1(cython.str bwtStr):
    """
    Takes a string output from bowtie and gets the names of the reads
    with MQ >= minMQ. Defaults to 1 (for a unique alignment)
    """
    cdef list lines, i
    cdef cython.str f
    cdef tuple nameCount
    return [RefKmer(i[0], contig=i[2], pos=int(i[3])) for
            i in [f.strip().split("\t") for
                  f in bwtStr.split("\n") if
                  f != "" and f[0] != "@"]
            if i[4] == "255"]


@cython.returns(list)
def GetMQPassReadsBwt1(cython.str bwtStr):
    """
    Takes a string output from bowtie and gets the names of the reads
    with MQ >= minMQ. Defaults to 1 (for a unique alignment)
    """
    cdef list lines, i
    cdef cython.str f
    cdef tuple nameCount
    return [i[0] for i in [f.strip().split("\t") for
                           f in bwtStr.split("\n") if
                           f != "" and f[0] != "@"]
            if i[4] == "255"]


@cython.returns(list)
def GetUniqMQsBowtie(cython.str bwtStr, cython.int minMQ=1):
    """
    Takes a string output from bowtie and gets the names of the reads
    with MQ >= minMQ. Defaults to 1 (for a unique alignment)
    """
    return GetUniqueItemsL(GetMQPassRefKmersBwt1(bwtStr))
