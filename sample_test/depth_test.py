#!/usr/bin/env python

if __name__ == "__main__":

    import unittest
    import sys
    from subprocess import check_output

    bampath = "FINAL_S376x3_Unpurified_GCATAACG-GCATAACG_L001_R1_001.R1.rsq.bam"
    bedpath = "ctDNA_V3_hotspot_SNVs_ROI_with_Colo829_dopes.bed"
    minFM = 2
    class Test(unittest.TestCase):
        def test_dmp_depth(self):
            samtools = int(float(check_output("echo $(samtools bedcov %s %s | cut -f4 | paste -sd+ | bc) / $(awk '{print $3 - $2}' %s | paste -sd+ | bc) | bc -l" % (bedpath, bampath, bedpath), shell=True).strip()))
            line = [i for i in
                    check_output("bmftools_db depth -b %s %s" % (bedpath, bampath),
                                 shell=True).split('\n') if
                    "Mean DMP Coverage" in i][0]
            bmftools = int(float(line.split(":")[1].split(".")[0].strip()))
            self.assertEqual(bmftools, samtools)

    unittest.main()
