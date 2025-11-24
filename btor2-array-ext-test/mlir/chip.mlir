  hw.module private @Queue8_BundleMap(in %clock : !seq.clock, in %reset : i1, out io_enq_ready : i1, in %io_enq_valid : i1, in %io_enq_bits_tl_state_size : i4, in %io_enq_bits_tl_state_source : i5, in %io_deq_ready : i1, out io_deq_valid : i1, out io_deq_bits_tl_state_size : i4, out io_deq_bits_tl_state_source : i5) {
    %c1_i3 = hw.constant 1 : i3
    %false = hw.constant false
    %c0_i3 = hw.constant 0 : i3
    %true = hw.constant true
    %ram = seq.firmem 0, 1, undefined, port_order {prefix = ""} : <8 x 9>
    seq.firmem.write_port %ram[%enq_ptr_value] = %1, clock %clock enable %8 : <8 x 9>
    %0 = seq.firmem.read_port %ram[%deq_ptr_value], clock %clock : <8 x 9>
    %1 = comb.concat %io_enq_bits_tl_state_source, %io_enq_bits_tl_state_size : i5, i4
    %2 = comb.extract %0 from 0 {sv.namehint = "ram_io_deq_bits_MPORT_data_tl_state_size"} : (i9) -> i4
    %3 = comb.extract %0 from 4 {sv.namehint = "ram_io_deq_bits_MPORT_data_tl_state_source"} : (i9) -> i5
    %enq_ptr_value = seq.firreg %11 clock %clock reset sync %reset, %c0_i3 {firrtl.random_init_start = 0 : ui64, sv.namehint = "enq_ptr_value"} : i3
    %deq_ptr_value = seq.firreg %13 clock %clock reset sync %reset, %c0_i3 {firrtl.random_init_start = 3 : ui64, sv.namehint = "deq_ptr_value"} : i3
    %maybe_full = seq.firreg %15 clock %clock reset sync %reset, %false {firrtl.random_init_start = 6 : ui64} : i1
    %4 = comb.icmp bin eq %enq_ptr_value, %deq_ptr_value {sv.namehint = "ptr_match"} : i3
    %5 = comb.xor bin %maybe_full, %true {sv.namehint = "_empty_T"} : i1
    %6 = comb.and bin %4, %5 {sv.namehint = "empty"} : i1
    %7 = comb.and bin %4, %maybe_full {sv.namehint = "full"} : i1
    %8 = comb.and bin %17, %io_enq_valid {sv.namehint = "do_enq"} : i1
    %9 = comb.and bin %io_deq_ready, %16 {sv.namehint = "do_deq"} : i1
    %10 = comb.add %enq_ptr_value, %c1_i3 {sv.namehint = "_value_T"} : i3
    %11 = comb.mux bin %8, %10, %enq_ptr_value : i3
    %12 = comb.add %deq_ptr_value, %c1_i3 {sv.namehint = "_value_T_2"} : i3
    %13 = comb.mux bin %9, %12, %deq_ptr_value : i3
    %14 = comb.icmp bin eq %8, %9 : i1
    %15 = comb.mux bin %14, %maybe_full, %8 : i1
    %16 = comb.xor bin %6, %true {sv.namehint = "io_deq_valid"} : i1
    %17 = comb.xor bin %7, %true {sv.namehint = "io_enq_ready"} : i1
    hw.output %17, %16, %2, %3 : i1, i1, i4, i5
  }