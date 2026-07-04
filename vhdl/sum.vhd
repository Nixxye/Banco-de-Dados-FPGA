library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity sum is
    Generic (
        DATA_WIDTH : integer := 8
    );
    Port (
        clk   : in  STD_LOGIC;
        rst   : in  STD_LOGIC;
        din   : in  STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0);
        dout  : out STD_LOGIC_VECTOR (31 downto 0);
        valid : in  STD_LOGIC
    );
end sum;

architecture Behavioral of sum is
    signal sum_value : unsigned(31 downto 0) := (others => '0');
begin
    process(clk, rst)
    begin
        if rst = '1' then
            sum_value <= (others => '0');
        elsif rising_edge(clk) then
            if valid = '1' then
                -- Subtract 48 (0x30) to convert ASCII digit to integer value
                sum_value <= sum_value + (unsigned(din) - 48);
            end if;
        end if;
    end process;
    
    dout <= std_logic_vector(sum_value);
end Behavioral;
