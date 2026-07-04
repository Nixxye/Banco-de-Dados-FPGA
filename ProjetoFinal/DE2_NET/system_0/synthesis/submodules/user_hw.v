module user_hw (
    input wire clk,
    input wire reset,
    
    // Avalon-MM Slave Interface
    input wire [6:0] address,
    input wire read,
    output reg [31:0] readdata,
    input wire write,
    input wire [31:0] writedata,
    input wire [3:0] byteenable
);

    // Memory map definitions
    localparam ADDR_CONTROL      = 7'd0; // 0x00
    localparam ADDR_DATA_IN      = 7'd1; // 0x04
    localparam ADDR_INSTRUCTION  = 7'd2; // 0x08
    localparam ADDR_STATUS       = 7'd3; // 0x0C
    localparam ADDR_DATA_OUT     = 7'd4; // 0x10
    localparam ADDR_ACCUMULATOR  = 7'd5; // 0x14
    localparam ADDR_SUM          = 7'd6; // 0x18

    reg read_prev;
    reg write_prev;
    
    always @(posedge clk or posedge reset) begin
        if (reset) begin
            read_prev <= 1'b0;
            write_prev <= 1'b0;
        end else begin
            read_prev <= read;
            write_prev <= write;
        end
    end
    
    wire write_pulse = write && !write_prev;

    // Data in and instruction registers
    reg [31:0] data_in_reg;
    reg [31:0] inst_reg;
    
    always @(posedge clk or posedge reset) begin
        if (reset) begin
            data_in_reg <= 32'd0;
            inst_reg <= 32'd0;
        end else begin
            if (write_pulse && (address == ADDR_DATA_IN)) begin
                if (byteenable[0]) data_in_reg[7:0]   <= writedata[7:0];
                if (byteenable[1]) data_in_reg[15:8]  <= writedata[15:8];
                if (byteenable[2]) data_in_reg[23:16] <= writedata[23:16];
                if (byteenable[3]) data_in_reg[31:24] <= writedata[31:24];
            end
            if (write_pulse && (address == ADDR_INSTRUCTION)) begin
                if (byteenable[0]) inst_reg[7:0]   <= writedata[7:0];
                if (byteenable[1]) inst_reg[15:8]  <= writedata[15:8];
                if (byteenable[2]) inst_reg[23:16] <= writedata[23:16];
                if (byteenable[3]) inst_reg[31:24] <= writedata[31:24];
            end
        end
    end

    reg din_valid_ctrl;
    reg load_inst;
    reg rd_out_fifo;
    reg sw_rst;
    reg eof_ctrl;
    
    always @(posedge clk or posedge reset) begin
        if (reset) begin
            din_valid_ctrl <= 1'b0;
            load_inst <= 1'b0;
            rd_out_fifo <= 1'b0;
            sw_rst <= 1'b0;
            eof_ctrl <= 1'b0;
        end else begin
            if (write_pulse && (address == ADDR_CONTROL)) begin
                din_valid_ctrl <= writedata[0];
                load_inst <= writedata[1];
                rd_out_fifo <= writedata[2];
                sw_rst <= writedata[3];
                if (writedata[4]) eof_ctrl <= 1'b1;
                if (writedata[3]) eof_ctrl <= 1'b0;
            end else begin
                din_valid_ctrl <= 1'b0;
                load_inst <= 1'b0;
                rd_out_fifo <= 1'b0;
                sw_rst <= 1'b0;
            end
        end
    end

    wire pipeline_rst = reset | sw_rst;

    wire full;
    wire empty;
    wire [31:0] out_fifo_dout;
    wire out_fifo_empty;
    wire out_fifo_full;
    wire [31:0] acc_out;
    wire [31:0] sum_out;
    wire done;

    pipeline #(
        .DATA_WIDTH(8),
        .NUM_COLS(4),
        .FIFO_DEPTH(128),
        .INSTRUCTION_WIDTH(32),
        .NUM_WHERE(4)
    ) inst_pipeline (
        .clk(clk),
        .rst(pipeline_rst),
        .din(data_in_reg),
        .din_valid(din_valid_ctrl),
        .eof(eof_ctrl),
        .full(full),
        .empty(empty),
        .instruction(inst_reg),
        .load_inst(load_inst),
        .rd_out_fifo(rd_out_fifo),
        .out_fifo_dout(out_fifo_dout),
        .out_fifo_empty(out_fifo_empty),
        .out_fifo_full(out_fifo_full),
        .acc_out(acc_out),
        .sum_out(sum_out),
        .in_fifo_count(),
        .out_fifo_count(),
        .done(done)
    );

    always @(*) begin
        readdata = 32'd0;
        if (read) begin
            case (address)
                ADDR_STATUS: begin
                    readdata[0] = out_fifo_full;
                    readdata[1] = out_fifo_empty;
                    readdata[2] = done;
                    readdata[3] = empty;
                    readdata[4] = full;
                end
                ADDR_DATA_OUT: begin
                    readdata = out_fifo_dout;
                end
                ADDR_ACCUMULATOR: begin
                    readdata = acc_out;
                end
                ADDR_SUM: begin
                    readdata = sum_out;
                end
                default: readdata = 32'd0;
            endcase
        end
    end

endmodule
