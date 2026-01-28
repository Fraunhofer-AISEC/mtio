# Copyright (c) 2007 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.params import *

from m5.objects.BaseSimpleCPU import BaseSimpleCPU


class BaseTimingSimpleCPU(BaseSimpleCPU):
    type = "BaseTimingSimpleCPU"
    cxx_header = "cpu/simple/timing.hh"
    cxx_class = "gem5::TimingSimpleCPU"

    tagged = Param.Bool(False, "Enable Tagging")
    shaded = Param.Bool(False, "Enable Shading")
    colorSize = Param.Int(4, "Size of the color")
    colorGranularity = Param.Int(16, "Size of color granularity")
    shadeGranularity = Param.Int(1, "Size of shade granularity")
    colorOffset = Param.Addr(0x100000000, "Offset of color memory")
    shadeSize = Param.Int(4, "Size of the shade")
    shadeOffset = Param.Addr(0x200000000, "Offset of shade memory")
    reportErrors = Param.Bool(False, "Report errors")

    @classmethod
    def memory_mode(cls):
        return "timing"

    @classmethod
    def support_take_over(cls):
        return True
