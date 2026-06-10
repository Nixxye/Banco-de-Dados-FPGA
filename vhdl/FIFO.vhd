library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity FIFO is
    Generic (
        DATA_WIDTH : integer := 8;  -- Tamanho de uma coluna
        NUM_COLS   : integer := 4;  -- Número de colunas
        DEPTH      : integer := 16  -- Número de linhas (profundidade)
    );
    Port (
        clk      : in  STD_LOGIC;
        rst      : in  STD_LOGIC;
        wr_en    : in  STD_LOGIC;
        rd_en    : in  STD_LOGIC;
        din      : in  STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
        dout     : out STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
        full     : out STD_LOGIC;
        empty    : out STD_LOGIC;
        data_count : out integer range 0 to DEPTH
    );
end FIFO;

architecture Behavioral of FIFO is
    type memory_type is array (0 to DEPTH-1) of STD_LOGIC_VECTOR((DATA_WIDTH * NUM_COLS)-1 downto 0);
    signal memory : memory_type := (others => (others => '0'));
    
    signal wr_ptr : integer range 0 to DEPTH-1 := 0;
    signal rd_ptr : integer range 0 to DEPTH-1 := 0;
    signal count  : integer range 0 to DEPTH := 0;
    
begin
    
    full <= '1' when count = DEPTH else '0';
    empty <= '1' when count = 0 else '0';
    data_count <= count;
    
    process(clk, rst)
    begin
        if rst = '1' then
            wr_ptr <= 0;
            rd_ptr <= 0;
            count <= 0;
            dout <= (others => '0');
        elsif rising_edge(clk) then
            -- Operação de escrita
            if wr_en = '1' and count < DEPTH then
                memory(wr_ptr) <= din;
                if wr_ptr = DEPTH - 1 then
                    wr_ptr <= 0;
                else
                    wr_ptr <= wr_ptr + 1;
                end if;
            end if;
            
            -- Operação de leitura
            if rd_en = '1' and count > 0 then
                dout <= memory(rd_ptr);
                if rd_ptr = DEPTH - 1 then
                    rd_ptr <= 0;
                else
                    rd_ptr <= rd_ptr + 1;
                end if;
            end if;
            
            -- Atualização do contador
            if (wr_en = '1' and count < DEPTH) and (rd_en = '0' or count = 0) then
                count <= count + 1;
            elsif (rd_en = '1' and count > 0) and (wr_en = '0' or count = DEPTH) then
                count <= count - 1;
            end if;
        end if;
    end process;
end Behavioral;
