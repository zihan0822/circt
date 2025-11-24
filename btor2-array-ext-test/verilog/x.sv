module array(
    input clk,
    output [7:0] out,
);
  reg [7:0] counter;
  reg [7:0] mem [7:0];

  always @(posedge clk) begin
    counter <= counter + 8'd1;
    mem[counter] <= counter + 8'd2;
  end
  assign out = mem[counter];

endmodule