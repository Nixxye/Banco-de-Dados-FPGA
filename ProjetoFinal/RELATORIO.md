# Relatório Parcial do Projeto Final (Atualizado)

Disciplina: Lógica Reconfigurável

Data: 16/06/2026

## 1. Objetivo do projeto

O objetivo deste projeto é implementar uma cadeia completa de consulta entre software e hardware reconfigurável na placa DE2. A aplicação recebe uma consulta a partir da interface web, localiza a tabela solicitada no cartão SD, envia dados ao hardware customizado e devolve o resultado ao navegador em formato legível.

No estado atual, o fluxo ponta a ponta com placa real está funcional para leitura de tabela e resposta HTTP. A parte de processamento do user_hw ainda está em estabilização de protocolo/formatação, e por isso existe fallback seguro para retorno direto dos dados do SD quando o hardware não devolve payload válido.

## 2. Interações com outros projetos e subsistemas

Este projeto interage com quatro frentes principais:

1. Projeto base da placa DE2 com Nios II:
   O sistema usa o projeto Quartus em DE2_NET, incluindo o processador Nios II, a interface Ethernet DM9000A e o BSP gerado para o ambiente Nios II EDS.

2. Projeto do pipeline em hardware:
   O módulo customizado user_hw recebe payload de tabela (e estrutura para descritor já prevista em software). O caminho de integração Nios-Avalon-user_hw está ativo e em depuração fina de protocolo de leitura/escrita.

3. Interface de usuário no navegador:
   A página em client/index.html permite enviar SQL, apontar o endpoint HTTP e visualizar o retorno formatado.

4. Ambiente de teste sem placa:
   Os arquivos tcpserver.py e table_format.py continuam disponíveis para teste local, mas o estado atual já inclui validação com placa real, rede real e SD real.

## 3. Descrição dos módulos

### 3.1. Módulos de hardware reconfigurável

Arquivo principal: DE2_NET/ip/user_hw/user_hw.v

Este módulo implementa a interface Avalon-MM usada pelo software no Nios II. No estado atual, ele opera com buffer interno de payload, sinais de status e caminho de leitura de saída após EOF, com ajustes recentes para evitar perdas por múltiplos ciclos de transação Avalon.

Funções principais do módulo:

1. Receber palavras de 32 bits do Nios II pela interface DATA_IN.
2. Controlar push, EOF e clear pelo registrador CONTROL.
3. Sinalizar buffer cheio, dado disponível, ocupado e término pelo registrador STATUS.
4. Entregar payload de saída para leitura pelo Nios II.
5. Manter compatibilidade com a futura implementação do pipeline real.

Observação: neste estágio o módulo autoral está em Verilog, não em VHDL.

### 3.2. Módulos em C no Nios II

Arquivos centrais:

1. DE2_NET/software/nios/main.c
2. DE2_NET/software/nios/network_tasks.c
3. DE2_NET/software/nios/network_tasks.h
4. DE2_NET/software/nios/sd_driver.c
5. DE2_NET/software/nios/sd_driver.h

Responsabilidades:

1. Inicializar a pilha de rede e o sistema operacional.
2. Subir o servidor HTTP na porta 80.
3. Receber requisições no formato TABLE/LIMIT/SQL.
4. Interpretar a consulta e localizar a tabela no SD real.
5. Enviar payload ao hardware customizado.
6. Capturar a saída do hardware e montar a resposta HTTP retornada ao navegador.
7. Aplicar fallback controlado para resposta direta do SD quando o hardware não devolve payload válido no tempo esperado.

Também fazem parte do subprojeto de software os arquivos de apoio já presentes no projeto base, como dm9000a.c, dm9000a.h e mac_ip_utils.c, que participam da infraestrutura de rede do Nios II.

### 3.3. Interação com o sistema operacional

O software embarcado usa MicroC/OS-II e a pilha NicheStack.

Interações principais:

1. Uma task inicial sobe a rede e aguarda o DHCP.
2. Depois disso, são criadas duas tasks de aplicação:
   rx_task: aceita conexões HTTP e armazena a requisição recebida.
   tx_task: processa a consulta e envia a resposta ao cliente.
3. A sincronização entre as tasks é feita com um semáforo do MicroC/OS-II.

Isso organiza a aplicação em uma etapa de recepção de rede e outra de processamento, reduzindo acoplamento entre socket e acesso ao hardware.

### 3.4. Módulos auxiliares fora da placa

1. client/index.html
   Interface web para enviar consultas e visualizar os resultados.

2. tcpserver.py
   Mock local do mesmo endpoint HTTP usado na placa.

3. table_format.py
   Implementa leitura e escrita do formato binário .tbl8.

4. prepare_sd_tables.py
   Gera tabelas de exemplo para testes locais e para preparação de arquivos no SD.

## 4. Detalhes de implementação

### 4.1. Protocolo HTTP atual

O firmware recebe um POST para /query com corpo texto no formato:

TABLE=nome_da_tabela
LIMIT=numero_maximo_de_linhas
SQL=consulta_sql_original

O retorno também é texto simples e contém chaves como STATUS, MODE, TABLE, SQL, SCANNED_ROWS, RETURNED_ROWS, COLUMNS e ROW.

### 4.2. Formato das tabelas

As tabelas são armazenadas em arquivos .tbl8.

Características principais:

1. Cada coluna ocupa 8 bytes fixos.
2. O cabeçalho contém magic, versão, número de colunas, número de linhas, largura da linha e nomes das colunas.
3. O payload contém as linhas em formato fixo, já alinhadas por campo.

O parser desse formato existe tanto no mock Python quanto no firmware embarcado. Na versão atual, a leitura é feita diretamente do SD real (cartão FAT32 validado em placa).

### 4.3. Ponte software-hardware

O caminho de ponte software-hardware já está operacional. A estrutura de descritor QRY1 foi implementada no firmware para compatibilidade com evolução do pipeline.

Estrutura geral prevista:

1. Palavra 0: magic 0x31595251
2. Palavra 1: tamanho total do descritor em bytes
3. Sequência de TLVs alinhados em 4 bytes
4. Em seguida, payload bruto da tabela

TLVs já implementados no firmware:

1. TABLE_NAME
2. RAW_SQL
3. SELECT_LIST
4. WHERE_CLAUSE
5. LIMIT
6. SOURCE_COLUMNS
7. SOURCE_COLUMN_COUNT
8. SOURCE_ROW_COUNT
9. SOURCE_ROW_WIDTH
10. SOURCE_CELL_SIZE

Essa estrutura foi criada para que a implementação futura do pipeline não exija alteração no software de integração.

### 4.4. Decisões ainda abertas

Os seguintes pontos foram deixados em aberto para a próxima etapa:

1. Quais operadores serão aceitos no WHERE.
2. Quantas condições máximas serão suportadas.
3. Como múltiplas condições serão combinadas.
4. Se o pipeline irá apenas filtrar linhas ou também projetar fisicamente as colunas da saída.

No estado atual, SELECT_LIST e WHERE_CLAUSE já chegam ao hardware como texto bruto quando o modo com descritor estiver ativo.

### 4.5. Limitações atuais

1. O processamento semântico completo de SQL (WHERE/SELECT projetado em hardware) ainda não está finalizado no user_hw.
2. Em alguns cenários, o user_hw ainda pode devolver payload vazio ou com empacotamento incorreto, acionando fallback de software.
3. O firmware atualmente prioriza robustez de demonstração (resposta válida via SD) sobre dependência obrigatória do resultado do hardware.
4. Há logs de depuração ativos no firmware para diagnóstico do caminho HW.

## 5. Estado atual de funcionamento

O fluxo completo foi validado com placa real DE2:

1. Programação da FPGA via Quartus/USB-Blaster validada.
2. Execução do firmware Nios II em hardware validada.
3. Rede Ethernet ativa com servidor HTTP na porta 80 validada.
4. Leitura de tabela .tbl8 em cartão SD (FAT32) validada.
5. Resposta HTTP de consulta retornando linhas corretas da tabela validada.

Exemplo observado em execução real:

STATUS=OK
MODE=board-sdcard-raw
TABLE=alunos
RETURNED_ROWS=4
ROW=00000001|ANA|COMP|9.5
ROW=00000002|BIA|ELEC|8.7
ROW=00000003|CARLOS|MEC|7.9
ROW=00000004|DAVI|COMP|9.9

O modo board-sdcard-raw indica que a fonte final da resposta foi o payload bruto do SD, usado como fallback seguro quando a saída do user_hw não está consistente.

## 6. Arquivos principais da entrega

1. README.txt
   Instruções objetivas para localizar arquivos e reconstruir o projeto.

2. DE2_NET/DE2_NET.qpf
   Projeto Quartus principal da parte de hardware.

3. DE2_NET/ip/user_hw/user_hw.v
   Módulo customizado usado como ponto de integração com o pipeline.

4. DE2_NET/software/nios
   Aplicação embarcada no Nios II.

5. DE2_NET/software/nios_bsp
   BSP do projeto Nios II.

6. client/index.html
   Interface web do sistema.

7. tcpserver.py
   Mock local para teste sem placa.

8. table_format.py
   Implementação do formato .tbl8.

9. DE2_NET/software/nios/sd_driver.c e sd_driver.h
   Driver SD (SPI bit-bang + leitura FAT) utilizado no firmware embarcado.

## 7. Conclusão

Nesta entrega parcial, a infraestrutura está funcional em hardware real: interface web, protocolo HTTP, rede na DE2, leitura de tabela em SD real e resposta de consulta no navegador. A integração com user_hw está operacional no barramento, porém ainda em fase de refinamento para garantir saída processada estável sem necessidade de fallback raw. O principal item da próxima fase é concluir a lógica de pipeline no user_hw e retirar os caminhos temporários de fallback/depuração.
