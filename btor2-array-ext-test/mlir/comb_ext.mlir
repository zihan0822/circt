module {
    hw.module @test() {
      %i = hw.constant 1: i1
      %out = comb.replicate %i : (i1) -> i16
    }
}
