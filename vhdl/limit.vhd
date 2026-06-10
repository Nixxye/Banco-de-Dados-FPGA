library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity limit is
    Generic (
        DATA_WIDTH : integer := 8;
        NUM_COLS   : integer := 4
    );
    Port (
        clk         : in  STD_LOGIC;
        rst         : in  STD_LOGIC;
        
        limit_value : in  STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0);
        active      : in  STD_LOGIC;
        
        din         : in  STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
        valid_in    : in  STD_LOGIC;
        
        dout        : out STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
        valid_out   : out STD_LOGIC;
        limit_hit   : out STD_LOGIC
    );
end limit;

architecture Behavioral of limit is
    signal count_value : unsigned(DATA_WIDTH-1 downto 0) := (others => '0');
    signal internal_hit : STD_LOGIC := '0';
begin
    process(clk, rst)
    begin
        if rst = '1' then
            count_value <= (others => '0');
            valid_out <= '0';
            dout <= (others => '0');
            internal_hit <= '0';
        elsif rising_edge(clk) then
            if active = '0' then
                -- Comportamento Transparente: Repassa dados instantaneamente e não bloqueia nada
                dout <= din;
                valid_out <= valid_in;
                internal_hit <= '0';
            else
                -- Passagem de dados pelo pipeline
                dout <= din;
                
                if valid_in = '1' and internal_hit = '0' then
                    count_value <= count_value + 1;
                    valid_out <= '1';
                    
                    if count_value + 1 >= unsigned(limit_value) then
                        internal_hit <= '1';
                    end if;
                else
                    valid_out <= '0';
                end if;
            end if;
        end if;
    end process;
    
    limit_hit <= internal_hit when active = '1' else '0';
end Behavioral;
