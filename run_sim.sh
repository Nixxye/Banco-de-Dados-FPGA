#!/bin/bash
echo "Compilando os arquivos VHDL..."
cd vhdl
ghdl -a FIFO.vhd count.vhd limit.vhd whereFilter.vhd pipeline.vhd tb_pipeline.vhd

echo "Elaborando o testbench..."
ghdl -e tb_pipeline

echo "Rodando a simulacao e gerando o VCD..."
ghdl -r tb_pipeline --vcd=wave.vcd --stop-time=1000ns

cd ..
echo "Abrindo GTKWave..."
gtkwave vhdl/wave.vcd config.gtkw &
echo "Simulacao finalizada!"
