"""
This is copied from gem5/configs/learning_gem5/part1/simple-riscv.py,
but uses the RiscvShadeCPU.

Run with
/gem5/build/RISCV/gem5.fast /configs/simple-riscv.py <binary> <args>
"""
import m5
from m5.objects import *
import sys
import os


class L1Cache(Cache):
    assoc = 8
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20

    def connectCPU(self, cpu):
        raise NotImplementedError

    def connectBus(self, bus):
        self.mem_side = bus.cpu_side_ports

class L2Cache(Cache):
    size = '256kB'
    assoc = 16
    tag_latency = 20
    data_latency = 20
    response_latency = 20
    mshrs = 20
    tgts_per_mshr = 12

    def connectCPUSideBus(self, bus):
        self.cpu_side = bus.mem_side_ports

    def connectMemSideBus(self, bus):
        self.mem_side = bus.cpu_side_ports

class L1ICache(L1Cache):
    size = '16kB'
    def connectCPU(self, cpu):
        self.cpu_side = cpu.icache_port

class L1DCache(L1Cache):
    size = '64kB'
    def connectCPU(self, cpu):
        self.cpu_side = cpu.dcache_port

cmd = []
for i in range(1, len(sys.argv)):
    cmd.append(sys.argv[i])

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"

FULL_MEM_SIZE = 0x800000000   # 32 GB in bytes
RESERVED_SIZE = 0x400000000   # 16 MB
MAIN_MEM_SIZE = FULL_MEM_SIZE - RESERVED_SIZE

system.mem_ranges = [AddrRange(0, size=MAIN_MEM_SIZE)]

system.cpu = RiscvTimingSimpleCPU()

system.membus = SystemXBar()

# system.cpu.icache_port = system.membus.cpu_side_ports
# system.cpu.dcache_port = system.membus.cpu_side_ports
# system.cpu.mtu.port = system.membus.cpu_side_ports

system.cpu.icache = L1ICache()
system.cpu.dcache = L1DCache()
system.cpu.icache.connectCPU(system.cpu)
system.cpu.dcache.connectCPU(system.cpu)

system.l2bus = L2XBar()
system.cpu.icache.connectBus(system.l2bus)
system.cpu.dcache.connectBus(system.l2bus)

system.membus = SystemXBar()
system.l2cache = L2Cache()
system.l2cache.connectCPUSideBus(system.l2bus)
system.l2cache.connectMemSideBus(system.membus)


system.cpu.createInterruptController()
system.cpu.tagged = True
system.cpu.shaded = False
system.cpu.colorOffset = 0x400000000
system.cpu.colorSize = 0x4
system.cpu.colorGranularity = 0x10
system.cpu.shadeOffset = 0x600000000
system.cpu.shadeGranularity = 0x1
system.cpu.shadeSize = 0x4
system.cpu.reportErrors = False

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR4_2400_16x4()
system.mem_ctrl.dram.range = AddrRange(0, size=FULL_MEM_SIZE)
system.mem_ctrl.port = system.membus.mem_side_ports

system.system_port = system.membus.cpu_side_ports

thispath = os.path.dirname(os.path.realpath(__file__))
cmd[0] = os.path.join(
        cmd[0]
        )

system.workload = SEWorkload.init_compatible(cmd[0])
# system.workload.wait_for_remote_gdb = True

process = Process()
process.cmd = cmd
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()

# exit_event = m5.simulate(1000000000000)
# exit_event = m5.simulate()

interval_ticks = 10000000000000  # 1 million ticks per interval

num_intervals = 10          # dump stats 10 times

for i in range(num_intervals):
    exit_event = m5.simulate(interval_ticks)
    # Dump the statistics to file
    m5.stats.dump()