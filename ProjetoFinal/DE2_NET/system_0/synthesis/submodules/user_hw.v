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

    // Mapa de registradores em palavras de 32 bits:
    // 0x00 (write): CONTROL
    //      bit 0 -> push DATA_IN na FIFO
    //      bit 1 -> sinaliza fim de entrada
    //      bit 2 -> limpa estado interno
    // 0x04 (write): DATA_IN
    // 0x08 (read): STATUS
    //      bit 0 -> FIFO cheia
    //      bit 1 -> DATA_OUT valido
    //      bit 2 -> ocupado
    //      bit 3 -> done
    // 0x0C (read): DATA_OUT

    localparam BUFFER_DEPTH = 128;
    localparam MAGIC_QUERY_STREAM = 32'h31595251;
    localparam ADDR_CONTROL = 7'd0;
    localparam ADDR_DATA_IN = 7'd1;
    localparam ADDR_STATUS = 7'd2;
    localparam ADDR_DATA_OUT = 7'd3;

    reg [31:0] data_in_reg;
    reg [31:0] payload_mem [0:BUFFER_DEPTH-1];
    reg [7:0] payload_count;
    reg [7:0] write_ptr;
    reg [7:0] read_ptr;
    reg read_prev;
    reg write_prev;
    reg eof_seen;
    reg descriptor_stream;
    reg descriptor_size_pending;
    reg [15:0] descriptor_skip_bytes;

    wire buffer_full = (payload_count == BUFFER_DEPTH[7:0]);
    wire output_valid = eof_seen && (read_ptr < payload_count);
    wire write_pulse = write && !write_prev;
    wire read_pulse = read && !read_prev;
    wire push_word = write_pulse && (address == ADDR_CONTROL) && writedata[0] && !buffer_full;
    wire pop_word = read_pulse && (address == ADDR_DATA_OUT) && output_valid;
    wire clear_all = write_pulse && (address == ADDR_CONTROL) && writedata[2];
    wire push_magic = push_word && !descriptor_stream && (data_in_reg == MAGIC_QUERY_STREAM);
    wire push_descriptor_size = push_word && descriptor_size_pending;
    wire push_descriptor_body = push_word && descriptor_stream && !descriptor_size_pending && (descriptor_skip_bytes != 16'd0);
    wire push_payload = push_word && !push_magic && !push_descriptor_size && !push_descriptor_body;
    
    always @(posedge clk or posedge reset) begin
        if (reset) begin
            data_in_reg <= 32'd0;
            payload_count <= 8'd0;
            write_ptr <= 8'd0;
            read_ptr <= 8'd0;
            read_prev <= 1'b0;
            write_prev <= 1'b0;
            eof_seen <= 1'b0;
            descriptor_stream <= 1'b0;
            descriptor_size_pending <= 1'b0;
            descriptor_skip_bytes <= 16'd0;
        end else begin
            read_prev <= read;
            write_prev <= write;

            if (clear_all) begin
                payload_count <= 8'd0;
                write_ptr <= 8'd0;
                read_ptr <= 8'd0;
                eof_seen <= 1'b0;
                descriptor_stream <= 1'b0;
                descriptor_size_pending <= 1'b0;
                descriptor_skip_bytes <= 16'd0;
            end else begin
                if (write_pulse && (address == ADDR_DATA_IN)) begin
                    if (byteenable[0]) data_in_reg[7:0] <= writedata[7:0];
                    if (byteenable[1]) data_in_reg[15:8] <= writedata[15:8];
                    if (byteenable[2]) data_in_reg[23:16] <= writedata[23:16];
                    if (byteenable[3]) data_in_reg[31:24] <= writedata[31:24];
                end

                if (write_pulse && (address == ADDR_CONTROL) && writedata[1]) begin
                    eof_seen <= 1'b1;
                end

                if (push_magic) begin
                    descriptor_stream <= 1'b1;
                    descriptor_size_pending <= 1'b1;
                    descriptor_skip_bytes <= 16'd0;
                end else if (push_descriptor_size) begin
                    descriptor_size_pending <= 1'b0;
                    if (data_in_reg[15:0] > 16'd8) begin
                        descriptor_skip_bytes <= data_in_reg[15:0] - 16'd8;
                    end else begin
                        descriptor_skip_bytes <= 16'd0;
                    end
                end else if (push_descriptor_body) begin
                    if (descriptor_skip_bytes > 16'd4) begin
                        descriptor_skip_bytes <= descriptor_skip_bytes - 16'd4;
                    end else begin
                        descriptor_skip_bytes <= 16'd0;
                    end
                end

                if (push_payload) begin
                    payload_mem[write_ptr] <= data_in_reg;
                    write_ptr <= write_ptr + 8'd1;
                    if (payload_count != BUFFER_DEPTH[7:0]) begin
                        payload_count <= payload_count + 8'd1;
                    end
                end

                if (pop_word) begin
                    read_ptr <= read_ptr + 8'd1;
                end
            end
        end
    end
    
    always @(*) begin
        readdata = 32'd0;
        if (read) begin
            if (address == ADDR_STATUS) begin
                readdata[0] = buffer_full;
                readdata[1] = output_valid;
                readdata[2] = !eof_seen;
                readdata[3] = eof_seen && (read_ptr >= payload_count);
                readdata[15:8] = payload_count - read_ptr;
            end else if (address == ADDR_DATA_OUT) begin
                if (output_valid) begin
                    readdata = payload_mem[read_ptr];
                end
            end
        end
    end

endmodule
