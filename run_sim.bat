@echo off
echo Compilando os arquivos VHDL...
cd vhdl
ghdl -a FIFO.vhd count.vhd limit.vhd whereFilter.vhd pipeline.vhd tb_pipeline.vhd
if %errorlevel% neq 0 exit /b %errorlevel%

echo Elaborando o testbench...
ghdl -e tb_pipeline
if %errorlevel% neq 0 exit /b %errorlevel%

echo Rodando a simulacao e gerando o VCD...
ghdl -r tb_pipeline --vcd=wave.vcd --stop-time=1000ns
if %errorlevel% neq 0 exit /b %errorlevel%

cd ..
echo Abrindo GTKWave...
gtkwave vhdl\wave.vcd config.gtkw
echo Simulacao finalizada!
pause
