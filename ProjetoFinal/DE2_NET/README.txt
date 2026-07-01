DE2_NET
-------

Este diretório não corresponde mais ao exemplo original de loopback Ethernet.
Ele foi adaptado para o Projeto Final da disciplina de Lógica Reconfigurável.

Projeto desenvolvido em dupla com Jean Carlos do Nascimento Cunha.

Pontos principais deste subprojeto:

1. Projeto Quartus:
   - DE2_NET.qpf
   - DE2_NET.qsf

2. Módulo customizado de integração com o pipeline:
   - ip/user_hw/user_hw.v

3. Projeto de software Nios II:
   - software/nios

4. BSP do Nios II:
   - software/nios_bsp

5. Arquivo de programação rápida da FPGA:
   - DE2_NET_time_limited.sof

O firmware embarcado recebe consultas HTTP, lê tabelas .tbl8 do cartão SD, conversa com o módulo user_hw e responde para a interface web.

O módulo user_hw atual ainda é um stub. O pipeline real deve ser implementado sobre esse ponto, preservando o mesmo mapa de registradores Avalon-MM.

Para a visão completa da entrega, arquivos principais e procedimento de reconstrução, consulte os documentos na raiz do repositório:

- ../README.TXT
- ../RELATORIO.md
