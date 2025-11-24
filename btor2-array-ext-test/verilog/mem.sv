module mem_enable(
    input clk,
    input en,
    input [2:0] addr,
    input [7:0] data_in,
    output [7:0] data_out
);
  reg [7:0] mem [7:0];
  wire [7:0] write_data;
  
  assign write_data = en ? data_in : mem[addr]; 
  
  always @(posedge clk) begin
    mem[addr] <= write_data;
  end
  
  assign data_out = mem[addr];
endmodule