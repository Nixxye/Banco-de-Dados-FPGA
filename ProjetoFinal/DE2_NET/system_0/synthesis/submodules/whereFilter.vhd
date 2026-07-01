library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity whereFilter is
    Generic (
        DATA_WIDTH : integer := 8
    );
    Port (
        clk   : in  STD_LOGIC;
        rst   : in  STD_LOGIC;
        op    : in  STD_LOGIC_VECTOR (2 downto 0); -- Operação de comparação - 000: ==, 001: !=, 010: <, 011: <=, 100: >, 101: >=
        value : in  STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0); -- Valor de comparação
        din   : in  STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0); -- Dados de entrada (dado da tabela)
        valid : out  STD_LOGIC
    );
end whereFilter;
architecture Behavioral of whereFilter is
    signal equal_result : STD_LOGIC;
    signal not_equal_result : STD_LOGIC;
    signal less_result : STD_LOGIC;
    signal less_equal_result : STD_LOGIC;
    signal greater_result : STD_LOGIC;
    signal greater_equal_result : STD_LOGIC;
    signal op_result : STD_LOGIC;
begin
    equal_result <= '1' when din = value else '0';
    not_equal_result <= '1' when din /= value else '0';
    less_result <= '1' when unsigned(din) < unsigned(value) else '0';
    less_equal_result <= '1' when unsigned(din) <= unsigned(value) else '0';
    greater_result <= '1' when unsigned(din) > unsigned(value) else '0';
    greater_equal_result <= '1' when unsigned(din) >= unsigned(value) else '0';

    op_result <= equal_result when op = "000" else
                 not_equal_result when op = "001" else
                 less_result when op = "010" else
                 less_equal_result when op = "011" else
                 greater_result when op = "100" else
                 greater_equal_result when op = "101" else '0';
    
    process(clk, rst)
    begin
        if rst = '1' then
            valid <= '0';
        elsif rising_edge(clk) then
            valid <= op_result;
        end if;
    end process;
end Behavioral;
