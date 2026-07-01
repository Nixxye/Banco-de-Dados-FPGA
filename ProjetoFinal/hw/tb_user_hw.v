`timescale 1ns/1ps

module tb_user_hw();

    // Inputs
    reg clk;
    reg reset;
    reg [6:0] address;
    reg read;
    reg write;
    reg [31:0] writedata;
    reg [3:0] byteenable;

    // Outputs
    wire [31:0] readdata;

    // Instantiate the Unit Under Test (UUT)
    user_hw uut (
        .clk(clk),
        .reset(reset),
        .address(address),
        .read(read),
        .readdata(readdata),
        .write(write),
        .writedata(writedata),
        .byteenable(byteenable)
    );

    // Clock generation
    always #10 clk = ~clk;

    // Task for writing to Avalon MM
    task avalon_write;
        input [6:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            address = addr;
            writedata = data;
            write = 1;
            byteenable = 4'b1111;
            @(posedge clk);
            write = 0;
            address = 0;
            writedata = 0;
            byteenable = 4'b0000;
        end
    endtask

    // Task for reading from Avalon MM
    task avalon_read;
        input [6:0] addr;
        output [31:0] data;
        begin
            @(posedge clk);
            address = addr;
            read = 1;
            @(posedge clk);
            data = readdata;
            read = 0;
            address = 0;
        end
    endtask

    reg [31:0] read_val;
    reg [31:0] expected_val;

    initial begin
        // Initialize Inputs
        clk = 0;
        reset = 1;
        address = 0;
        read = 0;
        write = 0;
        writedata = 0;
        byteenable = 0;

        // Wait 100 ns for global reset to finish
        #100;
        reset = 0;
        #20;

        $display("Starting Testbench...");

        // Escrever string de teste na memória (address 1)
        // Por exemplo: 8'h41 ('A'), 8'h42 ('B'), 8'hF7, 8'h00
        $display("Writing test data to address 1...");
        avalon_write(7'd1, {8'h41, 8'h42, 8'hF7, 8'h00});
        
        // Disparar a flag de start (address 0, bit 0)
        $display("Starting processing...");
        avalon_write(7'd0, 32'd1);

        // Aguardar o bit done (address 0, bit 1)
        read_val = 0;
        while ((read_val & 32'd2) == 0) begin
            avalon_read(7'd0, read_val);
            #10;
        end
        $display("Processing done flag detected!");

        // Ler resultado (address 1)
        avalon_read(7'd1, read_val);
        // Esperado: 
        // 8'h41 ('A') + 1 = 8'h42 ('B')
        // 8'h42 ('B') + 1 = 8'h43 ('C')
        // 8'hF7 -> 8'h00
        // 8'h00 -> 8'h00
        expected_val = {8'h42, 8'h43, 8'h00, 8'h00};
        
        if (read_val == expected_val) begin
            $display("TEST PASSED: Output matches expected value (%h)", read_val);
        end else begin
            $display("TEST FAILED: Expected %h, got %h", expected_val, read_val);
        end

        // Finish simulation
        #50;
        $finish;
    end

endmodule
