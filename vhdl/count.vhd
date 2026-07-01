library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity count is
    Generic (
        DATA_WIDTH : integer := 8
    );
    Port (
        clk   : in  STD_LOGIC;
        rst   : in  STD_LOGIC;
        din   : in  STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0);
        dout  : out STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0);
        valid : in  STD_LOGIC
    );
end count;
architecture Behavioral of count is
    signal count_value : unsigned(DATA_WIDTH-1 downto 0) := (others => '0');
begin
    process(clk, rst)
    begin
        if rst = '1' then
            count_value <= (others => '0');
        elsif rising_edge(clk) then
            if valid = '1' then
                count_value <= count_value + unsigned(din);
            end if;
        end if;
    end process;
    dout <= std_logic_vector(count_value);
end Behavioral;
