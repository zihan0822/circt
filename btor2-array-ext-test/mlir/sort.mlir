module {
    hw.module @test_array_create_const(in %clock : !seq.clock, out d: i16) {
      %c0 = hw.constant 0 : i16
      %i = hw.constant 0: i2
      %data = hw.constant 42 : i19
      %enable = hw.constant 1 : i1
      %mode = hw.constant 1 : i1
      // %index = hw.constant 1 : i2
      // %array = hw.array_create %c0, %c0, %c0, %c0: i16
      // %at0 = hw.array_get %array[%index] : !hw.array<4xi16>, i2
      %c = seq.firmem 0, 1, undefined, undefined : <3 x 19>
      // seq.firmem.write_port %c[%i] = %data, clock %clock : <3 x 19>
      // seq.firmem.read_port %c[%i], clock %clock : <3 x 19>
      // seq.firmem.write_port %c[%i] = %data, clock %clock : <3 x 19>
      %e = seq.firmem.read_write_port %c[%i] =  %data if %mode, clock %clock : <3 x 19>
      seq.firmem.read_port %c[%i], clock %clock : <3 x 19>
      hw.output %c0: i16
    }
}

// module {
//   hw.module @test(in %clock : !seq.clock, in %reset : i1, out o: i16) {
//     %c2 = hw.constant 2 : i16
//     %c3 = hw.constant 8 : i16
//     hw.output %c2: i16
//   }
// }
