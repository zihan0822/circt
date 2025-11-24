module mem_two_addr(
    input clk,
    input [1:0] wr_addr,
    input [1:0] rd_addr,
    input [7:0] data_in,
    output [7:0] data_out0,
    output [7:0] data_out1
);
  reg [7:0] mem0 [3:0];
  reg [7:0] mem1 [3:0];
  
  always @(posedge clk) begin
    mem0[wr_addr] <= data_in;
    mem1[wr_addr] <= data_in + 8'd7;
  end
  assign data_out0 = mem0[rd_addr];
  assign data_out1 = mem1[rd_addr];

endmodule