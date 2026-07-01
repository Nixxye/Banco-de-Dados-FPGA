PROJETO FINAL - LÓGICA RECONFIGURÁVEL

Este arquivo resume onde estão os componentes principais do trabalho e como reconstruir o projeto a partir do material entregue.

Projeto desenvolvido em dupla com Jean Carlos do Nascimento Cunha.

1. VISÃO GERAL

O projeto implementa uma cadeia de consulta entre navegador, firmware Nios II e hardware customizado na DE2.

Fluxo atual:
- a interface web envia uma consulta
- o firmware localiza a tabela e conversa com o hardware
- o hardware atual ainda é um stub de eco
- o resultado volta para a página HTML

2. ARQUIVOS PRINCIPAIS

- RELATORIO.md
  Relatório atualizado da entrega com objetivo, módulos, status atual e detalhes de implementação.

- DE2_NET/DE2_NET.qpf
  Projeto Quartus principal.

- DE2_NET/DE2_NET.qsf
  Configurações do projeto Quartus.

- DE2_NET/DE2_NET_time_limited.sof
  Arquivo .sof já gerado para programação rápida da FPGA.

- DE2_NET/ip/user_hw/user_hw.v
  Módulo de hardware customizado. Hoje funciona como stub e é o ponto onde o pipeline final deve ser implementado.

- DE2_NET/software/nios
  Projeto da aplicação Nios II.

- DE2_NET/software/nios_bsp
  BSP da aplicação Nios II.

- client/index.html
  Interface web de consulta.

- table_format.py
  Leitura e escrita do formato .tbl8.

3. FERRAMENTAS NECESSÁRIAS

- Quartus II / Quartus Prime compatível com o projeto DE2_NET
- Nios II EDS / Nios II SBT for Eclipse compatível com o BSP do projeto
- Navegador web para abrir client/index.html

O ambiente usado durante o desenvolvimento foi baseado no toolchain Altera 13.0sp1.

4. COMO RECONSTRUIR A PARTE DE HARDWARE

1. Abrir DE2_NET/DE2_NET.qpf no Quartus.
2. Conferir os arquivos do projeto em DE2_NET/.
3. Compilar o projeto no Quartus, se necessário.
4. Ou, para execução rápida, usar diretamente DE2_NET/DE2_NET_time_limited.sof no Programmer.

5. COMO RECONSTRUIR A PARTE DE SOFTWARE EMBARCADO

Opção recomendada por interface:

1. Abrir o Nios II SBT for Eclipse.
2. Selecionar uma workspace local qualquer.
3. Importar como Existing Projects into Workspace:
   - DE2_NET/software/nios
   - DE2_NET/software/nios_bsp
4. Fazer Build Project primeiro em nios_bsp.
5. Depois fazer Build Project em nios.
6. O ELF gerado fica em DE2_NET/software/nios/nios.elf.

6. COMO EXECUTAR COM A PLACA

1. No Quartus Programmer, selecionar o USB-Blaster.
2. Programar a FPGA com DE2_NET/DE2_NET_time_limited.sof.
3. No Nios II SBT for Eclipse, rodar o projeto nios em Nios II Hardware.
4. Acompanhar a inicialização no console do Eclipse.
5. Abrir client/index.html no navegador.
6. Trocar o endpoint para IP_DA_PLACA:80.
7. Executar a consulta.

7. INFORMAÇÕES IMPORTANTES PARA O PIPELINE

O software já envia ao hardware:
- o descritor de consulta QRY1
- a SQL bruta
- SELECT_LIST
- WHERE_CLAUSE
- LIMIT
- metadados do schema de entrada
- as linhas da tabela em blocos fixos

O módulo user_hw deve preservar o mesmo mapa de registradores para que não seja necessário alterar o firmware.

8. TAMANHO DA ENTREGA

Não há necessidade de armazenamento externo adicional nesta entrega. Os arquivos essenciais para reconstruir o projeto estão presentes no repositório.