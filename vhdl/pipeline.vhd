library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity pipeline is
    Generic (
        DATA_WIDTH : integer := 8;
        NUM_COLS   : integer := 4;
        FIFO_DEPTH : integer := 16;
        INSTRUCTION_WIDTH : integer := 32;
        NUM_WHERE : integer := 4
    );
    Port (
        clk            : in  STD_LOGIC;
        rst            : in  STD_LOGIC;
        
        -- Interface FIFO de Entrada
        din            : in  STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
        din_valid      : in  STD_LOGIC;
        full           : out STD_LOGIC;
        empty          : out STD_LOGIC;
        
        -- Interface de Instruções
        instruction    : in STD_LOGIC_VECTOR(INSTRUCTION_WIDTH-1 downto 0);
        load_inst      : in STD_LOGIC;
        
        -- Interface FIFO de Saída
        rd_out_fifo    : in  STD_LOGIC;
        out_fifo_dout  : out STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
        out_fifo_empty : out STD_LOGIC;
        out_fifo_full  : out STD_LOGIC;
        
        -- Acumulador
        acc_out        : out STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0);
        
        -- Contadores das FIFOs
        in_fifo_count  : out integer range 0 to FIFO_DEPTH;
        out_fifo_count : out integer range 0 to FIFO_DEPTH;
        
        -- Status do Pipeline
        done           : out STD_LOGIC
    );
end pipeline;

architecture Behavioral of pipeline is

    -- Componentes
    component FIFO is
        Generic (
            DATA_WIDTH : integer := 8;
            NUM_COLS   : integer := 4;
            DEPTH      : integer := 16
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
    end component;

    component count is
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
    end component;

    component limit is
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
            done_in     : in  STD_LOGIC;
            dout        : out STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
            valid_out   : out STD_LOGIC;
            limit_hit   : out STD_LOGIC;
            done_out    : out STD_LOGIC
        );
    end component;

    component whereFilter is
        Generic (
            DATA_WIDTH : integer := 8
        );
        Port (
            clk   : in  STD_LOGIC;
            rst   : in  STD_LOGIC;
            op    : in  STD_LOGIC_VECTOR (2 downto 0);
            value : in  STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0);
            din   : in  STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0);
            valid : out  STD_LOGIC
        );
    end component;

    type matriz_inst_tipo is array (0 to NUM_WHERE-1) of STD_LOGIC_VECTOR(INSTRUCTION_WIDTH-1 downto 0);
    type matriz_colum_tipo is array (0 to NUM_WHERE-1) of STD_LOGIC_VECTOR(DATA_WIDTH-1 downto 0);
    type col_array_t is array (0 to NUM_COLS-1) of STD_LOGIC_VECTOR(DATA_WIDTH-1 downto 0);

    -- Sinais internos da FIFO de entrada
    signal fifo_dout   : STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
    signal fifo_empty  : STD_LOGIC;
    signal fifo_full   : STD_LOGIC;
    signal fifo_rd_en  : STD_LOGIC;
    
    -- Sinais internos da FIFO de saída
    signal out_fifo_wr_en : STD_LOGIC;
    
    signal count_dout  : STD_LOGIC_VECTOR (DATA_WIDTH-1 downto 0);

    -- Sinais dos estágios do Pipeline Verdadeiro
    signal where_stage_dout : STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
    signal stage1_done      : STD_LOGIC := '0';
    signal rd_en_pipe1      : STD_LOGIC := '0';
    signal rd_en_pipe2      : STD_LOGIC := '0';
    
    signal limit_stage_dout : STD_LOGIC_VECTOR ((DATA_WIDTH * NUM_COLS)-1 downto 0);
    signal limit_valid_out  : STD_LOGIC;
    signal limit_hit        : STD_LOGIC;
    signal limit_done_out   : STD_LOGIC;
    signal limit_cols       : col_array_t;

    signal stage3_done      : STD_LOGIC := '0';
    signal running_flag     : STD_LOGIC := '0';

    -- Sinais para WHERE
    signal where_inst     : matriz_inst_tipo := (others => (others => '0'));
    signal where_colum    : matriz_colum_tipo;
    signal where_valid_out: STD_LOGIC_VECTOR(NUM_WHERE-1 downto 0);
    signal fifo_cols      : col_array_t;

    -- Registradores de configuração (Instruction Fetcher)
    signal where_active_cnt : integer range 0 to NUM_WHERE := 0;
    signal limit_val_reg    : STD_LOGIC_VECTOR(DATA_WIDTH-1 downto 0) := (others => '0');
    signal limit_active     : STD_LOGIC := '0';
    signal count_col_idx    : integer range 0 to NUM_COLS-1 := 0;
    signal count_active     : STD_LOGIC := '0';

    -- Sinais de fluxo e status
    signal where_valid_and : STD_LOGIC;
    signal count_valid_in  : STD_LOGIC;
    
    signal count_din       : STD_LOGIC_VECTOR(DATA_WIDTH-1 downto 0);

begin

    full <= fifo_full;
    empty <= fifo_empty;
    done <= stage3_done;

    -- Trava a leitura da FIFO enquanto estiver carregando instrução ou se já terminou na fonte (ou limit travou)
    fifo_rd_en <= '1' when (load_inst = '0' and fifo_empty = '0' and limit_hit = '0') else '0';

    -- Instruction Fetcher Process
    process(clk, rst)
        variable opcode : STD_LOGIC_VECTOR(3 downto 0);
    begin
        if rst = '1' then
            where_active_cnt <= 0;
            limit_active <= '0';
            count_active <= '0';
            for i in 0 to NUM_WHERE-1 loop
                where_inst(i) <= (others => '0');
            end loop;
        elsif rising_edge(clk) then
            if load_inst = '1' then
                opcode := instruction(INSTRUCTION_WIDTH-1 downto INSTRUCTION_WIDTH-4);
                
                if opcode = "0001" then -- WHERE
                    if where_active_cnt < NUM_WHERE then
                        where_inst(where_active_cnt) <= instruction;
                        where_active_cnt <= where_active_cnt + 1;
                    end if;
                
                elsif opcode = "0010" then -- LIMIT
                    limit_val_reg <= instruction(DATA_WIDTH-1 downto 0);
                    limit_active <= '1';
                
                elsif opcode = "0011" then -- COUNT
                    count_col_idx <= to_integer(unsigned(instruction(5 downto 0)));
                    count_active <= '1';
                end if;
            end if;
        end if;
    end process;

    -- Instância da FIFO de Entrada
    inst_FIFO: FIFO
        Generic map (
            DATA_WIDTH => DATA_WIDTH,
            NUM_COLS   => NUM_COLS,
            DEPTH      => FIFO_DEPTH
        )
        Port map (
            clk   => clk,
            rst   => rst,
            wr_en => din_valid,
            rd_en => fifo_rd_en,
            din   => din,
            dout  => fifo_dout,
            full  => fifo_full,
            empty => fifo_empty,
            data_count => in_fifo_count
        );

    -- Array de colunas da FIFO (fatiamento estático)
    gen_cols: for i in 0 to NUM_COLS-1 generate
        fifo_cols(i) <= fifo_dout(((i+1) * DATA_WIDTH) - 1 downto i * DATA_WIDTH);
    end generate;

    -- Instanciação genérica dos blocos whereFilter
    gen_where: for i in 0 to NUM_WHERE-1 generate
        where_colum(i) <= fifo_cols(to_integer(unsigned(where_inst(i)(DATA_WIDTH + 8 downto DATA_WIDTH + 3))));
        
        inst_where: whereFilter
            Generic map (
                DATA_WIDTH => DATA_WIDTH
            )
            Port map (
                clk   => clk,
                rst   => rst,
                op    => where_inst(i)(2 downto 0),
                value => where_inst(i)(DATA_WIDTH + 2 downto 3),
                din   => where_colum(i),
                valid => where_valid_out(i)
            );
    end generate;

    -- Lógica de AND para os filtros WHERE (apenas os ativos)
    process(where_valid_out, where_active_cnt, rd_en_pipe2)
        variable all_valid : STD_LOGIC;
    begin
        all_valid := rd_en_pipe2; -- Só é válido se foi oriundo de uma leitura real (valid bit pipeline)
        for i in 0 to NUM_WHERE-1 loop
            if i < where_active_cnt then
                all_valid := all_valid and where_valid_out(i);
            end if;
        end loop;
        where_valid_and <= all_valid;
    end process;

    -- Pipeline Stage 1: Alinha o dado original com a validação do WHERE e rastreia o EOF via read enable pipelining
    process(clk, rst)
    begin
        if rst = '1' then
            where_stage_dout <= (others => '0');
            rd_en_pipe1 <= '0';
            rd_en_pipe2 <= '0';
            stage1_done <= '0';
            running_flag <= '0';
        elsif rising_edge(clk) then
            if load_inst = '1' then
                running_flag <= '0';
                stage1_done <= '0';
            else
                where_stage_dout <= fifo_dout;
                rd_en_pipe1 <= fifo_rd_en;
                rd_en_pipe2 <= rd_en_pipe1;
                
                if fifo_rd_en = '1' then
                    running_flag <= '1';
                end if;
                
                -- O stage1_done sinaliza EOF apenas após o pipeline ter começado a rodar e a fila secar
                if running_flag = '1' and rd_en_pipe2 = '0' then
                    stage1_done <= '1';
                else
                    stage1_done <= '0';
                end if;
            end if;
        end if;
    end process;

    -- Instanciação do LIMIT repassando os dados do WHERE
    inst_limit: limit
        Generic map (
            DATA_WIDTH => DATA_WIDTH,
            NUM_COLS   => NUM_COLS
        )
        Port map (
            clk         => clk,
            rst         => rst,
            limit_value => limit_val_reg,
            active      => limit_active,
            din         => where_stage_dout,
            valid_in    => where_valid_and,
            done_in     => stage1_done,
            dout        => limit_stage_dout,
            valid_out   => limit_valid_out,
            limit_hit   => limit_hit,
            done_out    => limit_done_out
        );

    -- Fatiamento das colunas saindo do LIMIT para uso do COUNT
    gen_limit_cols: for i in 0 to NUM_COLS-1 generate
        limit_cols(i) <= limit_stage_dout(((i+1) * DATA_WIDTH) - 1 downto i * DATA_WIDTH);
    end generate;

    -- Fluxo para o COUNT usando saída do LIMIT e bloqueio pelo done token
    count_valid_in <= '1' when (limit_valid_out = '1' and limit_done_out = '0') else '0';
    count_din <= limit_cols(count_col_idx);

    inst_count: count
        Generic map (
            DATA_WIDTH => DATA_WIDTH
        )
        Port map (
            clk   => clk,
            rst   => rst,
            din   => count_din,
            valid => count_valid_in,
            dout  => count_dout
        );

    -- Configuração e Instância da FIFO de Saída recebendo dados do LIMIT
    -- O sinal limit_done_out garante o bloqueio imediato das escritas se o flush bater nesse estágio
    out_fifo_wr_en <= '1' when (limit_valid_out = '1' and load_inst = '0' and limit_done_out = '0') else '0';

    inst_out_FIFO: FIFO
        Generic map (
            DATA_WIDTH => DATA_WIDTH,
            NUM_COLS   => NUM_COLS,
            DEPTH      => FIFO_DEPTH
        )
        Port map (
            clk   => clk,
            rst   => rst,
            wr_en => out_fifo_wr_en,
            rd_en => rd_out_fifo,
            din   => limit_stage_dout,
            dout  => out_fifo_dout,
            full  => out_fifo_full,
            empty => out_fifo_empty,
            data_count => out_fifo_count
        );

    -- Pipeline Stage 3: Atraso final do sinal de Done para alinhamento com a FIFO de saída
    process(clk, rst)
    begin
        if rst = '1' then
            stage3_done <= '0';
        elsif rising_edge(clk) then
            if load_inst = '1' then
                stage3_done <= '0';
            else
                stage3_done <= limit_done_out;
            end if;
        end if;
    end process;

    -- Saída do Pipeline (o resultado do Acumulador)
    acc_out <= count_dout;

end Behavioral;
