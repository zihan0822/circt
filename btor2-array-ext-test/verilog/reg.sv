module array(
    input clk,
    input [7:0] in,
    output [7:0] out
);
  reg [7:0] data;

  always @(posedge clk) begin
    data <= in;
    out <= data;
  end

endmodule