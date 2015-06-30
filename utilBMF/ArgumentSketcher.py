import argparse
import cython
from warnings import warn

from operator import attrgetter as oag
from ._bmftools_helper import defaultConfig
from .HTSUtils import printlog as pl


@cython.returns(bint)
@cython.locals(input_str=cystr)
def to_bool(input_str):
    return (input_str.lower() == "true")

TypeConversionDict = {"s": str, "i": int, "f": float, "b": to_bool}


@cython.locals(lst=list, typechar=cystr,
               TypeConversionDict=dict)
def parseTuple(lst, TypeConversionDict=TypeConversionDict):
    assert(len(lst) == 2)
    try:
        typechar = lst[1][0]
    except IndexError:
        return lst[0]  # Is a string
    return TypeConversionDict[typechar](lst[0])


@cython.locals(path=cystr, parsedLines=list)
@cython.returns(dict)
def parseSketchConfig(path):
    """
    Parses in a file into a dictionary of key value pairs.

    Note: config style is key|value|typechar, where typechar
    is 'b' for bool, 'f' for float, 's' for string, and 'i' for int.
    Anything after a # character is ignored.
    """
    parsedLines = [l.strip().split("#")[0].split("|") for l in
                   open(path, "r").readlines()
                   if l[0] != "#"]
    # Note that the key is mangled to make the key match up with
    # argparse's name
    return {line[0].replace(" ", "_"): parseTuple([line[1], line[2]]) for
            line in parsedLines}


class ArgumentSketcher(object):
    """
    Class made for working equally easily with config files
    and command-line arguments.
    Loads the defaultConfig file, then overrides it with the parsed
    config file, and then overrides that with the command-line arguments.

    __init__:
    :param Namespace args - argparse's Namespace output
    :param cystr configPath - path to config file.

    Note: config style is key|value|typechar, where typechar
    is 'b' for bool, 'f' for float, 's' for string, and 'i' for int.
    Anything after a # character is ignored.
    """
    @cython.locals(key=cystr, oag=object)
    def arbitrate(self, oag=oag):
        for key in self.keys:
            value = getattr(self.args, key)
            if(value is not None):
                self.config[key] = value

    @cython.locals(args=object, config=cystr,
                   key=cystr, value=object)
    def __init__(self, args, configPath):
        # assert isinstance(args, argparse.Namespace)
        self.config = {key: value for key, value in
                       defaultConfig.iteritems()}
        # Load in config by copying each item so as to not give
        # ArgumentSketcher a dict subclass.
        try:
            tmpConfDict = parseSketchConfig(configPath)
        except TypeError:
            pl("No config path provided - skipping.")
            tmpConfDict = {}
        for key, value in tmpConfDict.iteritems():
            self.config[key] = value
        self.args = args
        self.keys = [i for i in dir(args) if i[0] != "_"]
        self.arbitrate()

    def __getitem__(self, item):
        return self.config[item]

    def __setitem__(self, key, value):
        self.config[key] = value

    @cython.locals(key=cystr)
    def __call__(self, key):
        return self.config[key]

    def values(self, *args, **kwargs):
        return self.config.values(*args, **kwargs)

    def itervalues(self, *args, **kwargs):
        return self.config.itervalues(*args, **kwargs)

    def iteritems(self, *args, **kwargs):
        return self.config.iteritems(*args, **kwargs)

    def items(self, *args, **kwargs):
        return self.config.items(*args, **kwargs)

    def getkeys(self, *args, **kwargs):
        return self.config.keys(*args, **kwargs)
