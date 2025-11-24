  hw.module private @TLDebugModuleOuter(in %clock : !seq.clock, in %reset : i1, out auto_dmi_in_a_ready : i1, in %auto_dmi_in_a_valid : i1, in %auto_dmi_in_a_bits_opcode : i3, in %auto_dmi_in_a_bits_address : i7, in %auto_dmi_in_a_bits_data : i32, in %auto_dmi_in_d_ready : i1, out auto_dmi_in_d_valid : i1, out auto_dmi_in_d_bits_opcode : i3, out auto_dmi_in_d_bits_data : i32, out auto_int_out_0 : i1, out io_ctrl_ndreset : i1, out io_ctrl_dmactive : i1, in %io_ctrl_dmactiveAck : i1, in %io_innerCtrl_ready : i1, out io_innerCtrl_valid : i1, out io_innerCtrl_bits_resumereq : i1, out io_innerCtrl_bits_hartsel : i10, out io_innerCtrl_bits_ackhavereset : i1, out io_innerCtrl_bits_hrmask_0 : i1, in %io_hgDebugInt_0 : i1) {
    %c0_i29 = hw.constant 0 : i29
    %c-4_i3 = hw.constant -4 : i3
    %c0_i10 = hw.constant 0 : i10
    %c1123200_i32 = hw.constant 1123200 : i32
    %c0_i32 = hw.constant 0 : i32
    %c0_i2 = hw.constant 0 : i2
    %false = hw.constant false
    %true = hw.constant true
    %DMCONTROLReg_haltreq = seq.firreg %5 clock %clock reset async %reset, %false {firrtl.random_init_start = 0 : ui64} : i1
    %DMCONTROLReg_hartsello = seq.firreg %3 clock %clock reset async %reset, %c0_i10 {firrtl.random_init_start = 6 : ui64, sv.namehint = "DMCONTROLReg_hartsello"} : i10
    %DMCONTROLReg_ndmreset = seq.firreg %2 clock %clock reset async %reset, %false {firrtl.random_init_start = 30 : ui64, sv.namehint = "DMCONTROLReg_ndmreset"} : i1
    %DMCONTROLReg_dmactive = seq.firreg %6 clock %clock reset async %reset, %false {firrtl.random_init_start = 31 : ui64, sv.namehint = "DMCONTROLReg_dmactive"} : i1
    %0 = comb.xor bin %DMCONTROLReg_dmactive, %true : i1
    %1 = comb.mux bin %31, %21, %DMCONTROLReg_ndmreset : i1
    %2 = comb.and %DMCONTROLReg_dmactive, %1 {sv.namehint = "DMCONTROLNxt_ndmreset"} : i1
    %3 = comb.mux bin %DMCONTROLReg_dmactive, %DMCONTROLReg_hartsello, %c0_i10 {sv.namehint = "DMCONTROLNxt_hartsello"} : i10
    %4 = comb.mux bin %31, %26, %DMCONTROLReg_haltreq : i1
    %5 = comb.and %DMCONTROLReg_dmactive, %4 {sv.namehint = "DMCONTROLNxt_haltreq"} : i1
    %6 = comb.mux bin %31, %20, %DMCONTROLReg_dmactive {sv.namehint = "DMCONTROLNxt_dmactive"} : i1
    %hrmaskReg_0 = seq.firreg %13 clock %clock reset async %reset, %false {firrtl.random_init_start = 64 : ui64} : i1
    %7 = comb.icmp bin eq %DMCONTROLReg_hartsello, %c0_i10 : i10
    %8 = comb.and bin %31, %22, %7 : i1
    %9 = comb.and bin %31, %23, %7 : i1
    %10 = comb.or %9, %hrmaskReg_0 : i1
    %11 = comb.or bin %0, %8 : i1
    %12 = comb.xor %11, %true : i1
    %13 = comb.and %12, %10 {sv.namehint = "hrmaskNxt_0"} : i1
    %14 = comb.and bin %DMCONTROLReg_dmactive, %io_ctrl_dmactiveAck {sv.namehint = "_out_prepend_T"} : i1
    %15 = comb.icmp bin eq %auto_dmi_in_a_bits_opcode, %c-4_i3 {sv.namehint = "in_bits_read"} : i3
    %16 = comb.extract %auto_dmi_in_a_bits_address from 4 {sv.namehint = "out_findex"} : (i7) -> i1
    %17 = comb.extract %auto_dmi_in_a_bits_address from 2 : (i7) -> i1
    %18 = comb.concat %16, %17 : i1, i1
    %19 = comb.icmp bin eq %18, %c0_i2 {sv.namehint = "_out_T_3"} : i2
    %20 = comb.extract %auto_dmi_in_a_bits_data from 0 {sv.namehint = "DMCONTROLWrData_dmactive"} : (i32) -> i1
    %21 = comb.extract %auto_dmi_in_a_bits_data from 1 {sv.namehint = "DMCONTROLWrData_ndmreset"} : (i32) -> i1
    %22 = comb.extract %auto_dmi_in_a_bits_data from 2 {sv.namehint = "DMCONTROLWrData_clrresethaltreq"} : (i32) -> i1
    %23 = comb.extract %auto_dmi_in_a_bits_data from 3 {sv.namehint = "DMCONTROLWrData_setresethaltreq"} : (i32) -> i1
    %24 = comb.extract %auto_dmi_in_a_bits_data from 28 {sv.namehint = "DMCONTROLWrData_ackhavereset"} : (i32) -> i1
    %25 = comb.extract %auto_dmi_in_a_bits_data from 30 {sv.namehint = "DMCONTROLWrData_resumereq"} : (i32) -> i1
    %26 = comb.extract %auto_dmi_in_a_bits_data from 31 {sv.namehint = "DMCONTROLWrData_haltreq"} : (i32) -> i1
    %27 = comb.concat %DMCONTROLReg_haltreq, %c0_i29, %DMCONTROLReg_ndmreset, %14 {sv.namehint = "out_prepend_11"} : i1, i29, i1, i1
    %28 = comb.extract %auto_dmi_in_a_bits_address from 3 {sv.namehint = "out_iindex"} : (i7) -> i1
    %29 = comb.xor %28, %true {sv.namehint = "out_backSel_0"} : i1
    %30 = comb.xor bin %15, %true {sv.namehint = "_out_wofireMux_T_1"} : i1
    %31 = comb.and bin %auto_dmi_in_a_valid, %auto_dmi_in_d_ready, %30, %29, %19 {sv.namehint = "out_woready_9"} : i1
    %32 = comb.mux bin %28, %c1123200_i32, %27 {sv.namehint = "_out_out_bits_data_T_3"} : i32
    %33 = comb.mux bin %19, %32, %c0_i32 {sv.namehint = "out_bits_data"} : i32
    %34 = comb.concat %c0_i2, %15 {sv.namehint = "dmiNodeIn_d_bits_opcode"} : i2, i1
    %debugIntRegs_0 = seq.firreg %37 clock %clock reset async %reset, %false {firrtl.random_init_start = 65 : ui64} : i1
    %35 = comb.or bin %debugIntRegs_0, %io_hgDebugInt_0 {sv.namehint = "intnodeOut_0"} : i1
    %36 = comb.mux bin %31, %26, %debugIntRegs_0 : i1
    %37 = comb.and %DMCONTROLReg_dmactive, %36 {sv.namehint = "debugIntNxt_0"} : i1
    %innerCtrlValidReg = seq.firreg %39 clock %clock reset async %reset, %false {firrtl.random_init_start = 66 : ui64} : i1
    %innerCtrlResumeReqReg = seq.firreg %40 clock %clock reset async %reset, %false {firrtl.random_init_start = 67 : ui64} : i1
    %innerCtrlAckHaveResetReg = seq.firreg %41 clock %clock reset async %reset, %false {firrtl.random_init_start = 68 : ui64} : i1
    %38 = comb.xor bin %io_innerCtrl_ready, %true {sv.namehint = "_innerCtrlAckHaveResetReg_T"} : i1
    %39 = comb.and bin %42, %38 {sv.namehint = "_innerCtrlValidReg_T_1"} : i1
    %40 = comb.and bin %44, %38 {sv.namehint = "_innerCtrlResumeReqReg_T_1"} : i1
    %41 = comb.and bin %46, %38 {sv.namehint = "_innerCtrlAckHaveResetReg_T_1"} : i1
    %42 = comb.or bin %31, %innerCtrlValidReg {sv.namehint = "io_innerCtrl_valid"} : i1
    %43 = comb.and bin %31, %25 {sv.namehint = "_io_innerCtrl_bits_resumereq_T"} : i1
    %44 = comb.or bin %43, %innerCtrlResumeReqReg {sv.namehint = "io_innerCtrl_bits_resumereq"} : i1
    %45 = comb.and bin %31, %24 {sv.namehint = "_io_innerCtrl_bits_ackhavereset_T"} : i1
    %46 = comb.or bin %45, %innerCtrlAckHaveResetReg {sv.namehint = "io_innerCtrl_bits_ackhavereset"} : i1
    hw.output %auto_dmi_in_d_ready, %auto_dmi_in_a_valid, %34, %33, %35, %DMCONTROLReg_ndmreset, %DMCONTROLReg_dmactive, %42, %44, %DMCONTROLReg_hartsello, %46, %13 : i1, i1, i3, i32, i1, i1, i1, i1, i1, i10, i1, i1
} 