circuit ArrayTest :
  module ArrayTest :
    input clock : Clock
    input reset : UInt<1>
    input addr : UInt<3>
    input wdata : UInt<64>
    input wen : UInt<1>
    input wmask : UInt<8>
    output rdata : UInt<64>
    
    ; Create a memory with 8 entries of 8-bit values
    mem mem :
      data-type => UInt<64>
      depth => 8
      read-latency => 0
      write-latency => 1
      reader => r
      writer => w

    ; Connect write port
    mem.w.addr <= addr
    mem.w.en <= wen
    mem.w.clk <= clock
    mem.w.data <= wdata
    mem.w.mask <= wmask 
    
    ; Connect read port
    mem.r.addr <= addr
    mem.r.en <= UInt<1>(1)
    mem.r.clk <= clock
    rdata <= mem.r.data
    