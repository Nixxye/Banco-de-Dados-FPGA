-- Lógica Reconfigurável

-- Wrapper para o controlador com uma interface avalon

LIBRARY IEEE;
USE IEEE.STD_LOGIC_1164.ALL;

ENTITY lcd_ctrl_avalon IS
	PORT
	(
		clk             :  IN  STD_LOGIC;
		rst_n           :  IN  STD_LOGIC;
		chipselect      :  IN  STD_LOGIC;
		write_en        :  IN  STD_LOGIC;
		read_en         :  IN  STD_LOGIC;
		writedata       :  IN  STD_LOGIC_VECTOR(31 DOWNTO 0);
		readdata        :  OUT STD_LOGIC_VECTOR(31 DOWNTO 0);
		rs_export       :  OUT STD_LOGIC;
		rw_export       :  OUT STD_LOGIC;
		en_export       :  OUT STD_LOGIC;
		lcd_data_export :  OUT STD_LOGIC_VECTOR(7 DOWNTO 0)
	);
END ENTITY lcd_ctrl_avalon;

ARCHITECTURE arch_lcd_ctrl_avalon OF lcd_ctrl_avalon IS

	COMPONENT lcd_controller IS
		PORT
		(
			 clk        : IN   STD_LOGIC;                     --system clock
			 reset_n    : IN   STD_LOGIC;                     --active low reinitializes lcd
			 lcd_enable : IN   STD_LOGIC;                     --latches data into lcd controller
			 lcd_bus    : IN   STD_LOGIC_VECTOR(9 DOWNTO 0);  --data and control signals
			 busy       : OUT  STD_LOGIC := '1';              --lcd controller busy/idle feedback
			 rw, rs, e  : OUT  STD_LOGIC;                     --read/write, setup/data, and enable for lcd
			 lcd_data   : OUT  STD_LOGIC_VECTOR(7 DOWNTO 0)   --data signals for lcd
		);
	END COMPONENT;

	SIGNAL local_lcd_enable : STD_LOGIC;
	SIGNAL local_lcd_bus    : STD_LOGIC_VECTOR(9 DOWNTO 0);
	SIGNAL local_busy       : STD_LOGIC;
	
BEGIN

	lcd_ctrl : lcd_controller PORT MAP(
		clk        => clk,
		reset_n    => rst_n,
		lcd_enable => local_lcd_enable,
		lcd_bus    => local_lcd_bus,
		busy       => local_busy,
		rw         => rw_export,
		rs         => rs_export,
		e          => en_export,
		lcd_data   => lcd_data_export);

	local_lcd_enable <= write_en AND chipselect;
	local_lcd_bus <= writedata(9 downto 0);
	readdata <= (31 DOWNTO 1 => '0') & local_busy;
		
END ARCHITECTURE arch_lcd_ctrl_avalon;
