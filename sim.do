# Encerra a simulação anterior para evitar crashs/bugs na interface do ModelSim
quit -sim

# Cria e mapeia a biblioteca de trabalho
vlib work
vmap work work

# Compila todos os arquivos na ordem correta
vcom -work work vhdl/FIFO.vhd
vcom -work work vhdl/count.vhd
vcom -work work vhdl/limit.vhd
vcom -work work vhdl/whereFilter.vhd
vcom -work work vhdl/pipeline.vhd
vcom -work work vhdl/tb_pipeline.vhd

# Inicia a simulação
# Nota: algumas versões mais novas do ModelSim/Questa exigem -voptargs=+acc no lugar de -novopt para não cortar os sinais internos.
vsim -voptargs=+acc work.tb_pipeline

# Adiciona as ondas com divisores (Dividers) para organização
add wave -divider "SYSTEM"
add wave sim:/tb_pipeline/clk
add wave sim:/tb_pipeline/rst
add wave sim:/tb_pipeline/done

add wave -divider "INSTRUCTION FETCHER"
add wave sim:/tb_pipeline/load_inst
add wave -hex sim:/tb_pipeline/instruction

add wave -divider "INPUT FIFO"
add wave sim:/tb_pipeline/din_valid
add wave sim:/tb_pipeline/empty
add wave sim:/tb_pipeline/full
add wave -unsigned sim:/tb_pipeline/in_fifo_count
add wave -hex sim:/tb_pipeline/din

add wave -divider "WHERE FILTER 0"
add wave -unsigned sim:/tb_pipeline/uut/gen_where(0)/inst_where/din
add wave -unsigned sim:/tb_pipeline/uut/gen_where(0)/inst_where/value
add wave -binary sim:/tb_pipeline/uut/gen_where(0)/inst_where/op
add wave sim:/tb_pipeline/uut/gen_where(0)/inst_where/valid

add wave -divider "WHERE FILTER 1"
add wave -unsigned sim:/tb_pipeline/uut/gen_where(1)/inst_where/din
add wave -unsigned sim:/tb_pipeline/uut/gen_where(1)/inst_where/value
add wave -binary sim:/tb_pipeline/uut/gen_where(1)/inst_where/op
add wave sim:/tb_pipeline/uut/gen_where(1)/inst_where/valid

add wave -divider "FLUSH TOKENS"
add wave sim:/tb_pipeline/uut/stage1_done
add wave sim:/tb_pipeline/uut/limit_done_out
add wave sim:/tb_pipeline/uut/stage3_done

add wave -divider "WHERE RESULTS"
add wave sim:/tb_pipeline/uut/where_valid_and
add wave -unsigned sim:/tb_pipeline/uut/where_active_cnt

add wave -divider "LIMIT FILTER"
add wave sim:/tb_pipeline/uut/limit_active
add wave sim:/tb_pipeline/uut/limit_valid_out
add wave sim:/tb_pipeline/uut/limit_hit
add wave -unsigned sim:/tb_pipeline/uut/limit_stage_dout

add wave -divider "COUNT ACCUMULATOR"
add wave sim:/tb_pipeline/uut/count_active
add wave sim:/tb_pipeline/uut/count_valid_in
add wave -unsigned sim:/tb_pipeline/uut/count_din
add wave -unsigned sim:/tb_pipeline/acc_out

add wave -divider "OUTPUT FIFO"
add wave sim:/tb_pipeline/rd_out_fifo
add wave sim:/tb_pipeline/out_fifo_empty
add wave sim:/tb_pipeline/out_fifo_full
add wave -unsigned sim:/tb_pipeline/out_fifo_count
add wave -hex sim:/tb_pipeline/out_fifo_dout

# Roda 1 microsegundo de simulação
run 1 us

# Enquadra a tela
wave zoom full

add wave -divider "OUTPUT/DONE FLAGS"
add wave sim:/tb_pipeline/uut/done