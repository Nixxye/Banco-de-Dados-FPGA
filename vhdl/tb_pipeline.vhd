library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity tb_pipeline is
end tb_pipeline;

architecture sim of tb_pipeline is

    constant DATA_WIDTH : integer := 8;
    constant NUM_COLS   : integer := 4;
    constant FIFO_DEPTH : integer := 16;
    constant INSTRUCTION_WIDTH : integer := 32;
    constant NUM_WHERE : integer := 4;

    signal clk            : STD_LOGIC := '0';
    signal rst            : STD_LOGIC := '0';
    signal din            : STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0) := (others => '0');
    signal din_valid      : STD_LOGIC := '0';
    signal full           : STD_LOGIC;
    signal empty          : STD_LOGIC;
    signal instruction    : STD_LOGIC_VECTOR(INSTRUCTION_WIDTH-1 downto 0) := (others => '0');
    signal load_inst      : STD_LOGIC := '0';
    signal rd_out_fifo    : STD_LOGIC := '0';
    signal out_fifo_dout  : STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
    signal out_fifo_empty : STD_LOGIC;
    signal out_fifo_full  : STD_LOGIC;
    signal acc_out        : STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0);
    signal in_fifo_count  : integer range 0 to FIFO_DEPTH;
    signal out_fifo_count : integer range 0 to FIFO_DEPTH;
    signal done           : STD_LOGIC;

    constant CLK_PERIOD : time := 10 ns;

begin

    uut: entity work.pipeline
        Generic map (
            DATA_WIDTH => DATA_WIDTH,
            NUM_COLS   => NUM_COLS,
            FIFO_DEPTH => FIFO_DEPTH,
            INSTRUCTION_WIDTH => INSTRUCTION_WIDTH,
            NUM_WHERE => NUM_WHERE
        )
        Port map (
            clk            => clk,
            rst            => rst,
            din            => din,
            din_valid      => din_valid,
            full           => full,
            empty          => empty,
            instruction    => instruction,
            load_inst      => load_inst,
            rd_out_fifo    => rd_out_fifo,
            out_fifo_dout  => out_fifo_dout,
            out_fifo_empty => out_fifo_empty,
            out_fifo_full  => out_fifo_full,
            acc_out        => acc_out,
            in_fifo_count  => in_fifo_count,
            out_fifo_count => out_fifo_count,
            done           => done
        );

    clk_process: process
    begin
        clk <= '0';
        wait for CLK_PERIOD/2;
        clk <= '1';
        wait for CLK_PERIOD/2;
    end process;

    stim_proc: process
    begin
        -- 1. Reset
        rst <= '1';
        wait for CLK_PERIOD * 2;
        rst <= '0';
        wait for CLK_PERIOD * 2;

        -- 2. Carregar Instruções
        
        -- WHERE 1: Coluna 1 == 5
        -- opcode=0001, op=000, value=5, col_idx=1
        instruction <= (others => '0');
        instruction(31 downto 28) <= "0001";
        instruction(16 downto 11) <= "000001"; -- col_idx = 1
        instruction(10 downto 3)  <= x"05";    -- value = 5
        instruction(2 downto 0)   <= "000";    -- op = ==
        load_inst <= '1';
        wait for CLK_PERIOD;

        -- WHERE 2: Coluna 2 <= 10
        -- opcode=0001, op=011 (<=), value=10, col_idx=2
        instruction <= (others => '0');
        instruction(31 downto 28) <= "0001";
        instruction(16 downto 11) <= "000010"; -- col_idx = 2
        instruction(10 downto 3)  <= x"0A";    -- value = 10
        instruction(2 downto 0)   <= "011";    -- op = <=
        load_inst <= '1';
        wait for CLK_PERIOD;

        -- LIMIT: Parar em 3
        -- opcode=0010, value=3
        instruction <= (others => '0');
        instruction(31 downto 28) <= "0010";
        instruction(7 downto 0)   <= x"03";    -- limit = 3
        load_inst <= '1';
        wait for CLK_PERIOD;

        -- COUNT: Somar coluna 3
        -- opcode=0011, col_idx=3
        instruction <= (others => '0');
        instruction(31 downto 28) <= "0011";
        instruction(5 downto 0)   <= "000011"; -- col_idx = 3
        load_inst <= '1';
        wait for CLK_PERIOD;

        load_inst <= '0';
        wait for CLK_PERIOD * 2;

        -- 3. Injetar Dados na FIFO (col3 | col2 | col1 | col0)
        -- Data 1: amt=100 (x64), price=8, cat=5, id=1 -> MATCH
        din <= x"64" & x"08" & x"05" & x"01";
        din_valid <= '1';
        wait for CLK_PERIOD;

        -- Data 2: amt=50 (x32), price=15(x0F), cat=5, id=2 -> FALHA (price > 10)
        din <= x"32" & x"0F" & x"05" & x"02";
        din_valid <= '1';
        wait for CLK_PERIOD;

        -- Data 3: amt=200 (xC8), price=5, cat=1, id=3 -> FALHA (cat != 5)
        din <= x"C8" & x"05" & x"01" & x"03";
        din_valid <= '1';
        wait for CLK_PERIOD;

        -- Data 4: amt=30 (x1E), price=2, cat=5, id=4 -> MATCH
        din <= x"1E" & x"02" & x"05" & x"04";
        din_valid <= '1';
        wait for CLK_PERIOD;

        -- Data 5: amt=10 (x0A), price=10(x0A), cat=5, id=5 -> MATCH (LIMIT HIT)
        din <= x"0A" & x"0A" & x"05" & x"05";
        din_valid <= '1';
        wait for CLK_PERIOD;

        -- Data 6: amt=90 (x5A), price=1, cat=5, id=6 -> IGNORADO PELO LIMIT
        din <= x"5A" & x"01" & x"05" & x"06";
        din_valid <= '1';
        wait for CLK_PERIOD;

        din_valid <= '0';

        -- 4. Esperar pipeline processar e disparar 'done'
        wait until done = '1';
        wait for CLK_PERIOD * 3;

        -- 5. Ler as saídas filtradas
        rd_out_fifo <= '1';
        wait for CLK_PERIOD * 5;
        rd_out_fifo <= '0';

        wait;
    end process;

end sim;
