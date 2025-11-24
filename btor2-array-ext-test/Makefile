.PHONY: all clean
	
VERILOGS := $(wildcard verilog/*.sv)
MLIRS := $(wildcard mlir/*.mlir)
FIRRTL := $(wildcard firrtl/*.fir)
BTORS := $(VERILOGS:.sv=.btor) $(MLIRS:.mlir=.btor) $(FIRRTL:.fir=.btor)

all: $(BTORS)
	
%.fir: %.sv
	yosys -p "read_verilog -sv $<; proc;; write_firrtl $@"

%.mlir: %.fir
	firtool --ir-hw $< > $@

%.btor: %.mlir
	circt-opt --convert-hw-to-btor2 $< > $@
	
clean:
	rm -f verilog/*.btor mlir/*.btor firrtl/*.btor
