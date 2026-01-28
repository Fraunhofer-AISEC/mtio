import m5
from m5.objects import Root

from gem5.utils.requires import requires
from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.cpu_types import CPUTypes
from gem5.isas import ISA
from gem5.simulate.simulator import Simulator
from gem5.resources.workload import Workload

# This runs a check to ensure the gem5 binary is compiled for RISCV.

requires(isa_required=ISA.RISCV)

from gem5.components.cachehierarchies.tag_cache_hierarchy import (
    TaggedCacheHierarchy,
)

cache_hierarchy = TaggedCacheHierarchy()

# Memory: Dual Channel DDR4 2400 DRAM device.
memory = DualChannelDDR4_2400(size="16GB")

# Here we setup the processor. We use a simple processor.
processor = SimpleProcessor(cpu_type=CPUTypes.TAG, isa=ISA.RISCV, num_cores=1)

# Here we setup the board. The RiscvBoard allows for Full-System RISCV
# simulations.
board = RiscvBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# Here we a full system workload: "riscv-ubuntu-20.04-boot" which boots
# Ubuntu 20.04. Once the system successfully boots it encounters an `m5_exit`
# instruction which stops the simulation. When the simulation has ended you may
# inspect `m5out/system.pc.com_1.device` to see the stdout.
board.set_workload(Workload("riscv-ubuntu-20.04-boot"))

simulator = Simulator(board=board)
simulator.run()
