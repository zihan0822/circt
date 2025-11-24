module {
    hw.module @test_array_create() {
      %c0 = hw.constant 0 : i16
      %index = hw.constant 1 : i2
      %array = hw.array_create %c0, %c0, %c0, %c0: i16
      %at0 = hw.array_get %array[%index] : !hw.array<4xi16>, i2
      %aggregate = hw.aggregate_constant [1 : i3, -3: i3]: !hw.array<2xi3>
    }
}