import pysam

from MawCluster.PileupUtils import PCInfo, AlleleAggregateInfo
from utilBMF import HTSUtils
from utilBMF.HTSUtils import printlog as pl, ThisIsMadness

"""
This module contains a variety of tools for calling variants.
Currently, it primarily works with SNPs primarily with experimental
features present for structural variants
TODO: Filter based on variants supported by reads going both ways.
TODO: Make calls for SNPs, not just reporting frequencies.
Reverse Strand Fraction - RSF
Both Strands Support Variant - BS
Fraction of unmerged reads supporting variant - TF
Allele Fraction - AF
DP - Depth (Merged)
"""


class VCFLine:

    def __init__(self,
                 AltAggregateObject,
                 MaxPValue=float("1e-30"),
                 ID=".",
                 DOCMerged="default",
                 DOCTotal="default",
                 TotalFracStr="default",
                 MergedFracStr="default",
                 TotalCountStr="default",
                 MergedCountStr="default",
                 FailedBQReads="default",
                 FailedMQReads="default",
                 minNumFam=2):
        if(isinstance(AltAggregateObject, AlleleAggregateInfo) is False):
            raise HTSUtils.ThisIsMadness("VCFLine requires an AlleleAgg"
                                         "regateInfo for initialization")
        if(DOCMerged == "default"):
            raise HTSUtils.ThisIsMadness("DOC (Merged) required!")
        if(DOCTotal == "default"):
            raise HTSUtils.ThisIsMadness("DOC (Total) required!")
        self.CHROM = AltAggregateObject.contig
        self.POS = AltAggregateObject.pos
        self.CONS = AltAggregateObject.consensus
        self.ALT = AltAggregateObject.ALT
        self.QUAL = AltAggregateObject.SumBQScore
        if(AltAggregateObject.BothStrandSupport is True):
            self.QUAL *= 2
            # This is entirely arbitrary...
        self.ID = ID
        try:
            if(float(MaxPValue) > 10 ** (self.QUAL / -10)):
                if(AltAggregateObject.BothStrandSupport is True and
                   AltAggregateObject.MergedReads >= minNumFam):
                    self.FILTER = "PASS"
                elif(AltAggregateObject.BothStrandSupport is True):
                    self.FILTER = "Too few families supporting variant"
                else:
                    self.FILTER = "OneStrandSupport"
            else:
                self.FILTER = "LowQual"
        except TypeError:
            print("TypeError! MaxPValue is: {}".format(MaxPValue))
            raise TypeError("MaxPValue not properly parsed.")
        if(self.ALT == AltAggregateObject.consensus):
            self.FILTER = "CONSENSUS"
        self.InfoFields = {"AF": AltAggregateObject.MergedReads
                           / float(AltAggregateObject.DOC),
                           "TF": AltAggregateObject.TotalReads
                           / float(AltAggregateObject.DOCTotal),
                           "BS": AltAggregateObject.BothStrandSupport,
                           "RSF": AltAggregateObject.ReverseMergedReads
                           / float(AltAggregateObject.MergedReads),
                           "MQM": AltAggregateObject.AveMQ,
                           "MQB": AltAggregateObject.AveBQ,
                           "MMQ": AltAggregateObject.minMQ,
                           "MBQ": AltAggregateObject.minBQ,
                           "QA": AltAggregateObject.SumBQScore,
                           "NUMALL": AltAggregateObject.NUMALT,
                           "BQF": FailedBQReads,
                           "MQF": FailedMQReads,
                           "TYPE": "snp",
                           "PVC": MaxPValue,
                           "CONS": self.CONS}
        if(TotalCountStr != "default"):
            self.InfoFields["TACS"] = TotalCountStr
        if(TotalFracStr != "default"):
            self.InfoFields["TAFS"] = TotalFracStr
        if(MergedCountStr != "default"):
            self.InfoFields["MACS"] = MergedCountStr
        if(MergedFracStr != "default"):
            self.InfoFields["MAFS"] = MergedFracStr
        self.InfoStr = ";".join(
            ["=".join([key, str(self.InfoFields[key])])
             for key in self.InfoFields.keys()])
        self.FormatFields = {"DP": DOCMerged,
                             "DPA": AltAggregateObject.MergedReads,
                             "DPT": DOCTotal}
        self.FormatStr = (":".join(self.FormatFields.keys()) + "\t" +
                          ":".join(str(self.FormatFields[key])
                                   for key in self.FormatFields.keys()))
        self.str = "\t".join([str(i) for i in [self.CHROM,
                                               self.POS,
                                               self.ID,
                                               self.CONS,
                                               self.ALT,
                                               self.QUAL,
                                               self.FILTER,
                                               self.InfoStr,
                                               self.FormatStr]])

    def update(self):
        self.FormatKey = ":".join(self.FormatFields.keys())
        self.FormatValue = ":".join([str(self.FormatFields[key])
                                     for key in self.FormatFields.keys()])
        self.FormatStr = (":".join(self.FormatFields.keys()) + "\t" +
                          ":".join(str(self.FormatFields[key])
                                   for key in self.FormatFields.keys()))
        self.InfoStr = ";".join([key + "=" + str(self.InfoFields[key])
                                 for key in self.InfoFields.keys()])

    def ToString(self):
        self.update()
        self.str = "\t".join([str(i) for i in [self.CHROM,
                                               self.POS,
                                               self.ID,
                                               self.CONS,
                                               self.ALT,
                                               self.QUAL,
                                               self.FILTER,
                                               self.InfoStr,
                                               self.FormatStr]])
        return self.str


class VCFPos:

    def __init__(self, PCInfoObject,
                 MaxPValue=1e-18,
                 keepConsensus=True,
                 reference="default"):
        if(isinstance(PCInfoObject, PCInfo) is False):
            raise HTSUtils.ThisIsMadness("VCFPos requires an "
                                         "PCInfo for initialization")
        if(reference == "default"):
            HTSUtils.FacePalm("VCFPos requires a reference fasta.")
        # Because bamtools is 0-based but vcfs are 1-based
        self.pos = PCInfoObject.pos + 1
        self.minMQ = PCInfoObject.minMQ
        self.consensus = PCInfoObject.consensus
        self.TotalFracStr = PCInfoObject.TotalFracStr
        self.MergedFracStr = PCInfoObject.MergedFracStr
        self.TotalCountStr = PCInfoObject.TotalCountStr
        self.MergedCountStr = PCInfoObject.MergedCountStr
        self.REF = pysam.FastaFile(reference).fetch(
            PCInfoObject.contig, self.pos - 1, self.pos)

        self.VCFLines = [VCFLine(
            alt, TotalCountStr=self.TotalCountStr,
            MergedCountStr=self.MergedCountStr,
            TotalFracStr=self.TotalFracStr,
            MergedFracStr=self.MergedFracStr,
            DOCMerged=PCInfoObject.MergedReads,
            DOCTotal=PCInfoObject.TotalReads,
            MaxPValue=MaxPValue,
            FailedBQReads=PCInfoObject.FailedBQReads,
            FailedMQReads=PCInfoObject.FailedMQReads)
            for alt in PCInfoObject.AltAlleleData]
        self.keepConsensus = keepConsensus
        if(self.keepConsensus is True):
            self.str = "\n".join([line.ToString()
                                  for line in
                                  self.VCFLines])
        else:
            self.str = "\n".join([line.ToString()
                                  for line in
                                  self.VCFLines
                                  if line.FILTER != "CONSENSUS"])

    def ToString(self):
        [line.update() for line in self.VCFLines]
        if(self.keepConsensus is True):
            self.str = "\n".join([line.ToString()
                                  for line in
                                  self.VCFLines])
        else:
            self.str = "\n".join([line.ToString()
                                  for line in
                                  self.VCFLines
                                  if line.FILTER != "CONSENSUS"])
        return self.str


class HeaderFileFormatLine:
    """
    This class holds the fileformat line in a VCF.
    Defaults to VCFv4.1
    """

    def __init__(self, fileformat="VCFv4.1"):
        if(fileformat == "default"):
            self.fileformat = "VCFv4.1"
        else:
            self.fileformat = fileformat

    def ToString(self):
        self.str = "##fileformat={}".format(self.fileformat)
        return self.str


class HeaderInfoLine:
    """
    This class holds a VCF INFO header line.
    """
    def __init__(self, ID="default", Number="default", Type="default",
                 Description="default"):
        Types = "integer,float,string,flag,character,string"
        if(Type.lower() not in Types.lower().split(",")):
            raise ThisIsMadness("Invalid Type provided!")
        self.Type = Type

        Numbers = "A,G"
        try:
            self.Number = int(Number)
        except ValueError:
            if(Number) in Numbers.split(','):
                self.Number = Number
            else:
                raise ThisIsMadness("Invalid \"Number\" provided.")

        self.ID = ID.upper()

        if(Description != "default"):
            self.Description = Description
        else:
            raise ThisIsMadness("A description is required.")

    def ToString(self):
        self.str = ("##INFO=<ID="
                    "{},Number={},Type=".format(self.ID, self.Number) +
                    "{},Description={}>".format(self.Type, self.Description))
        return self.str


class HeaderFormatLine:
    """
    This class holds a VCF FORMAT header line.
    """
    def __init__(self, ID="default", Number="default", Type="default",
                 Description="default"):
        Types = "integer,float,string,character"
        if(Type.lower() not in Types.lower().split(",")):
            raise ThisIsMadness("Invalid Type provided!")
        self.Type = Type

        Numbers = "A,G"
        try:
            self.Number = int(Number)
        except ValueError:
            if(Number) in Numbers.split(','):
                self.Number = Number
            else:
                raise ThisIsMadness("Invalid \"Number\" provided.")

        self.ID = ID.upper()

        if(Description != "default"):
            self.Description = Description
        else:
            raise ThisIsMadness("A description is required.")

    def ToString(self):
        self.str = ("##FORMAT=<ID="
                    "{},Number={},Type=".format(self.ID, self.Number) +
                    "{},Description={}>".format(self.Type, self.Description))
        return self.str


class HeaderAltLine:
    """
    This class holds a VCF ALT header line.
    """
    def __init__(self, ID="default", Description="default"):
        Types = "DEL,INS,DUP,INV,CNV,DUP:TANDEM,DEL:ME,INS:ME"
        if(ID.upper() in Types.split(",")):
            self.ID = ID.upper()
        else:
            raise ThisIsMadness("VCF4.1 Specifications require that"
                                " this field be one of the following:"
                                " {}".format(Types.replace(",", ", ")))
        self.Description = Description

    def ToString(self):
        self.str = "##ALT=<ID={},Description={}>".format(self.ID,
                                                         self.Description)
        return self.str


class HeaderFilterLine:
    """
    This class holds a VCF FILTER header line.
    """
    def __init__(self, ID="default", Description="default"):
        self.ID = ID
        self.Description = Description

    def ToString(self):
        self.str = "##FILTER=<ID={},Description={}>".format(
            self.ID, self.Description)
        return self.str


class HeaderAssemblyLine:
    """
    This class holds a VCF assembly header line.
    """
    def __init__(self, assembly="default"):
        self.assembly = assembly

    def ToString(self):
        self.str = "##assembly={}".format(self.assembly)
        return self.str


class HeaderCommandLine:
    """
    This class holds a VCF command header line.
    """
    def __init__(self, commandStr="default"):
        self.commandStr = commandStr

    def ToString(self):
        self.str = "##commandline={}".format(self.commandStr)
        return self.str


class HeaderReferenceLine:
    """
    This class holds a VCF Reference header line.
    """
    def __init__(self, reference="default", isfile=False):
        self.reference = reference
        if(isinstance(isfile, bool) is True):
            self.isfile = isfile
        elif(isinstance(isfile, str) is True):
            if("true" in isfile.lower()):
                self.isfile = True
            elif("false" in isfile.lower()):
                self.isfile = False
            else:
                raise ValueError(
                    "isfile kwarg provided was not true or false!")
        else:
            raise ValueError(
                "isfile kwarg provided was not true or false!")

    def ToString(self):
        if(self.isfile is False):
            self.str = "##reference={}".format(self.reference)
        else:
            self.str = "##reference=file://{}".format(self.reference)
        return self.str


class HeaderContigLine:
    """
    This class holds a VCF Assembly header line.
    """
    def __init__(self, contig="default", length="default"):
        self.contig = contig
        try:
            self.length = int(length)
        except ValueError:
            raise ThisIsMadness("Length must be an integer!")

    def ToString(self):
        self.str = "##contig=<ID={},length={}>".format(
            self.contig, self.length)
        return self.str


"""
This next section contains the dictionaries which hold the
FILTER Header entries
Valid tags: PASS,OneStrandSupport,LowQual
"""

HeaderFilterDict = {}
HeaderFilterDict["PASS"] = HeaderFilterLine(ID="PASS",
                                            Description="All filters passed")
HeaderFilterDict["OneStrandSupport"] = HeaderFilterLine(
    ID="OneStrandSupport",
    Description="High quality but only supported by reads on one strand")
HeaderFilterDict["LowQual"] = HeaderFilterLine(
    ID="LowQual", Description="Low Quality, below P-Value Cutoff")
HeaderFilterDict["CONSENSUS"] = HeaderFilterLine(
    ID="CONSENSUS",
    Description="Not printed due to being the "
    "consensus sequence, not somatic. (Unless the consensus is cancerous, "
    "in which case the patient is dead in the water.)")


"""
This next section contains the dictionaries which hold the
INFO Header entries
Valid tags: AF,TF,BS,RSF,MQM,MQB,MMQ,MBQ,QA,NUMALL,BQF,MQF,
TYPE,PVC,TACS,TAFS,MACS,MAFS
"""

HeaderInfoDict = {}
HeaderInfoDict["AF"] = HeaderInfoLine(
    ID="AF",
    Type="Float",
    Description=("Allele Frequency, for each ALT allele, "
                 "in the same order as listed for Merged read families."),
    Number="A")
HeaderInfoDict["TF"] = HeaderInfoLine(
    ID="TF",
    Type="Float",
    Description=("Total Allele Frequency, for each ALT allele, "
                 "in the same order as listed for unmerged read families,"
                 "IE, without having removed the duplicates."),
    Number="A")
HeaderInfoDict["BS"] = HeaderInfoLine(
    ID="BS",
    Type="String",
    Description="Variant supported by reads mapping to both strands.",
    Number="A")
HeaderInfoDict["RSF"] = HeaderInfoLine(
    ID="RSF",
    Type="Float",
    Description=("Fraction of reads supporting allele "
                 "aligned to reverse strand."),
    Number="A")
HeaderInfoDict["MQM"] = HeaderInfoLine(ID="MQM",
                                       Description="Mean Quality for Mapping",
                                       Number="A",
                                       Type="Float")

HeaderInfoDict["MQB"] = HeaderInfoLine(ID="MQB",
                                       Description="Mean Base Quality",
                                       Number="A",
                                       Type="Float")
HeaderInfoDict["MMQ"] = HeaderInfoLine(
    ID="MMQ",
    Description="Min Mapping Quality for reads used in variant calling.",
    Number="A",
    Type="Integer")
HeaderInfoDict["MBQ"] = HeaderInfoLine(
    ID="MBQ",
    Description="Min Base Quality for reads used in variant calling.",
    Number="A",
    Type="Integer")
HeaderInfoDict["QA"] = HeaderInfoLine(
    ID="QA",
    Description="Alternate allele quality sum in phred",
    Number="A",
    Type="Integer")
HeaderInfoDict["NUMALL"] = HeaderInfoLine(
    ID="NUMALL",
    Description="Number of unique alleles at position.",
    Number=1,
    Type="Integer")
HeaderInfoDict["BQF"] = HeaderInfoLine(
    ID="BQF",
    Description="Number of (merged) reads failed for low BQ.",
    Number=1,
    Type="Integer")
HeaderInfoDict["MQF"] = HeaderInfoLine(
    ID="MQF",
    Description="Number of (merged) reads failed for low MQ.",
    Number=1,
    Type="Integer")
HeaderInfoDict["TYPE"] = HeaderInfoLine(
    ID="TYPE",
    Description="The type of allele, either snp, mnp, ins, del, or complex.",
    Number="A",
    Type="String")
HeaderInfoDict["PVC"] = HeaderInfoLine(
    ID="PVC",
    Description="P-value cutoff used.",
    Number=1,
    Type="Float")
HeaderInfoDict["TACS"] = HeaderInfoLine(
    ID="TACS",
    Description="Total (Unmerged) Allele Count String.",
    Number=1,
    Type="String")
HeaderInfoDict["TAFS"] = HeaderInfoLine(
    ID="TAFS",
    Description="Total (Unmerged) Allele Frequency String.",
    Number=1,
    Type="String")
HeaderInfoDict["MACS"] = HeaderInfoLine(
    ID="MACS",
    Description="Merged Allele Count String.",
    Number=1,
    Type="String")
HeaderInfoDict["MAFS"] = HeaderInfoLine(
    ID="MAFS",
    Description="Merged Allele Frequency String.",
    Number=1,
    Type="String")

"""
This next section contains the dictionaries which hold the
FORMAT Header entries
Valid tags: DP, DPA, DPT
"""

HeaderFormatDict = {}
HeaderFormatDict["DP"] = HeaderFormatLine(
    ID="DP",
    Type="Integer",
    Description=("Merged Read Depth."),
    Number=1)
HeaderFormatDict["DPA"] = HeaderFormatLine(
    ID="DPA",
    Type="Integer",
    Description=("Merged Read Depth for Allele."),
    Number="A")
HeaderFormatDict["DPT"] = HeaderFormatLine(
    ID="DPT",
    Type="Integer",
    Description=("Unmerged Read Depth for Allele."),
    Number="A")


def GetContigHeaderLines(header):
    """
    This program creates the string for the contig lines
    in a VCF from the pysam.AlignmentFile attribute "header".
    """
    if(isinstance(header, dict) is False):
        raise ThisIsMadness("Requires pysam.AlignmentFile header dictionary")
    contigLineList = []
    for contigDict in header['SQ']:
        contigLineList.append(
            HeaderContigLine(contig=contigDict["SN"],
                             length=contigDict["LN"]).ToString())
    return "\n".join(contigLineList)


def GetVCFHeader(fileFormat="default", FILTERTags="default",
                 commandStr="default",
                 reference="default", reference_is_path=False,
                 header="default",
                 INFOTags="default",
                 FORMATTags="default"
                 ):
    HeaderLinesStr = ""
    # fileformat line
    HeaderLinesStr += HeaderFileFormatLine(
        fileformat=fileFormat).ToString() + "\n"
    # FILTER lines
    if(FILTERTags == "default"):
        for key in HeaderFilterDict.keys():
            HeaderLinesStr += HeaderFilterDict[key].ToString() + "\n"
    else:
        for filter in FILTERTags.split(","):
            try:
                HeaderLinesStr += HeaderFilterDict[
                    filter.upper()].ToString() + "\n"
            except KeyError:
                pl("Filter {} not found - continuing.".format(filter))
    # INFO lines
    if(INFOTags == "default"):
        for key in HeaderInfoDict.keys():
            HeaderLinesStr += HeaderInfoDict[key].ToString() + "\n"
    else:
        for info in INFOTags.split(","):
            try:
                HeaderLinesStr += HeaderInfoDict[
                    info.upper()].ToString() + "\n"
            except KeyError:
                pl("Info {} not found - continuing.".format(info))
    # FORMAT lines
    if(FORMATTags == "default"):
        for key in HeaderFormatDict.keys():
            HeaderLinesStr += HeaderFormatDict[key].ToString() + "\n"
    else:
        for format in FORMATTags.split(","):
            try:
                HeaderLinesStr += HeaderFormatDict[
                    format.upper()].ToString() + "\n"
            except KeyError:
                pl("Format {} not found - continuing.".format(format))
    # commandline line
    if(commandStr != "default"):
        HeaderLinesStr += HeaderCommandLine(
            commandStr=commandStr).ToString() + "\n"
    # reference line
    if(reference != "default"):
        HeaderLinesStr += HeaderReferenceLine(
            reference=reference, isfile=reference_is_path).ToString() + "\n"
    # contig lines
    HeaderLinesStr += GetContigHeaderLines(header) + "\n"
    HeaderLinesStr += "\t".join(["#CHROM", "POS",
                                 "ID", "REF",
                                 "ALT", "QUAL",
                                 "FILTER", "INFO"]) + "\n"
    return HeaderLinesStr