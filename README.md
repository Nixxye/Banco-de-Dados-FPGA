# Banco de Dados Acelerado em FPGA - Altera DE-2/Cyclone 10

Este diretorio contem a versao consolidada da documentacao e dos materiais finais do projeto da disciplina de Logica Reconfiguravel.

## Objetivo resumido

O sistema recebe consultas via TCP/IP na placa DE-2, carrega tabelas .tbl8 do SD card, traduz o SQL para instrucoes de 32 bits e envia os dados para um acelerador em FPGA. O hardware aplica filtros WHERE, LIMIT e agregacoes COUNT/SUM, devolvendo o resultado por HTTP.

## Arquivos essenciais do projeto

### 1. Hardware / FPGA
* DE2_NET/DE2_NET.qpf
* DE2_NET/DE2_NET.qsf
* DE2_NET/system_0.qsys
* DE2_NET/DE2_NET.v
* DE2_NET/system_0_inst.v
* DE2_NET/ip/user_hw/user_hw.v
* vhdl/FIFO.vhd
* vhdl/whereFilter.vhd
* vhdl/limit.vhd
* vhdl/count.vhd
* vhdl/sum.vhd
* vhdl/pipeline.vhd

### 2. Software embarcado
* DE2_NET/software/nios/main.c
* DE2_NET/software/nios/network_tasks.c
* DE2_NET/software/nios/network_tasks.h
* DE2_NET/software/nios/sd_driver.c
* DE2_NET/software/nios/dm9000a.c
* DE2_NET/software/nios/mac_ip_utils.c
* DE2_NET/software/nios_bsp/

### 3. Testes e suporte
* 	cpserver.py
* pga_test.py
* 	able_format.py
* mock_sd/tables/
* client/index.html

### 4. Documentacao consolidada
* RELATORIO_FINAL.md

## Mapeamento de armazenamento / uso

### 1. SD card da placa
* Armazena as tabelas .tbl8 consumidas pelo firmware.
* Os arquivos podem ficar na raiz do cartao ou em /tables, conforme o codigo em C.

### 2. Rede Ethernet
* Utilizada pelo DM9000A da DE-2.
* O firmware atende requisicoes HTTP na porta 80.

### 3. Mock local no PC
* 	cpserver.py simula a interface HTTP.
* 	able_format.py define e le o formato .tbl8 usado pelos testes.

## Como Compilar e Rodar o Projeto do Zero (Build Guide)

### Parte 1: Hardware (Quartus Prime)
**Passo 1:** Abra o Quartus Prime e carregue o projeto DE2_NET.qpf localizado na pasta ProjetoFinal/.
**Passo 2:** No menu superior, va em *Processing > Start Compilation*. Aguarde a sintese completa do hardware VHDL.
**Passo 3:** Conecte a placa DE2-115 via cabo USB e ligue-a.
**Passo 4:** Abra o Programmer (*Tools > Programmer*), certifique-se de que o Hardware Setup esta como "USB-Blaster", selecione o arquivo gerado DE2_NET_time_limited.sof e clique em *Start*.

### Parte 2: Software (Nios II Eclipse)
**Passo 5:** Abra o Nios II Software Build Tools for Eclipse e selecione a pasta 
ios_workspace/ como seu workspace.
**Passo 6:** Se os projetos nao estiverem visiveis, va em *File > Import > Nios II Software Build Tools Project* e importe as pastas DE2_NET/software/nios e DE2_NET/software/nios_bsp.
**Passo 7:** Clique com o botao direito no projeto 
ios_bsp > *Nios II > Generate BSP*.
**Passo 8:** Selecione o projeto 
ios e aperte Ctrl+B (Build Project).
**Passo 9:** Com a compilacao finalizada sem erros, clique com o botao direito no projeto 
ios > *Run As > Nios II Hardware*. Anote o Endereco de IP exibido no Console.

### Parte 3: Cliente Web (Frontend HTML)
**Passo 10:** Navegue ate a pasta client/.
**Passo 11:** Abra o arquivo index.html no navegador web.
**Passo 12:** Insira o Endereco de IP que apareceu no passo 9 (ex: 192.168.0.x:80).
**Passo 13:** Digite sua consulta SQL e clique em *Executar Query*.

### Solucao de Problemas e Erros Comuns

**Erro: "Project nios_bsp is out of date" ou Falha no Build do Eclipse**
* **Causa:** O Eclipse perdeu a referencia dos caminhos.
* **Solucao:** Botao direito no 
ios_bsp, *Nios II > Generate BSP*. Depois Clean Project em ambos e recompile.

**Erro: Quartus Programmer diz "Failed" ao tentar enviar o .sof**
* **Causa:** Placa desligada, cabo solto, ou drivers do USB-Blaster nao instalados.
* **Solução:** Verifique o Gerenciador de Dispositivos e instale o driver apontando para a pasta C:\intelFPGA\...\quartus\drivers.

**Erro: Nios Console exibe "Error: SD Card not found"**
* **Causa:** Cartao SD nao encaixado ou nao formatado corretamente.
* **Solucao:** O cartao SD deve estar em FAT16. O arquivo .tbl deve estar na raiz. Insira o cartao antes de rodar o codigo C.

**Erro de Conexao na Pagina Web (CORS ou Timeout)**
* **Causa:** IP digitado errado ou Nios travado.
* **Solucao:** Verifique no Console do Eclipse se pegou IP valido e se ha resposta de ping.

## Como usar

1. Envie POST para /query.
2. O corpo da requisicao deve conter linhas como:
   - TABLE=alunos
   - LIMIT=10
   - SQL=SELECT * FROM alunos WHERE ... LIMIT ...
3. O firmware responde em texto puro com STATUS=OK ou STATUS=ERROR.
4. Para testes em PC, use 	cpserver.py e pga_test.py.

## Notas de integracao

1. O modulo user_hw expoe registradores Avalon-MM de 32 bits para controle, dados, instrucoes, status e resultados.
2. O pipeline VHDL trabalha com no maximo 4 condicoes WHERE por consulta.
3. COUNT e SUM usam acumuladores de 32 bits.
4. O SD e acessado via SPI bit-bang.
5. O firmware usa uC/OS-II e NicheStack, nao pthreads.

