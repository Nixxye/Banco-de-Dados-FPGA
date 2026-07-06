# Diagramas do Projeto

Estes diagramas ilustram a arquitetura geral e as partes específicas do nosso sistema de Banco de Dados na FPGA. Como a nossa interface aqui suporta o formato **Mermaid**, os diagramas já devem estar renderizados visualmente para você. 

Para colocar no seu Google Docs, você pode simplesmente tirar um *print screen* (captura de tela) da imagem gerada aqui, ou copiar os blocos de texto e jogar no site [Mermaid Live Editor](https://mermaid.live/) para baixar em PNG/SVG de alta qualidade.

## 1. Visão Geral do Sistema (System Overview)

Este diagrama mostra o fluxo geral entre os principais atores do nosso projeto:

```mermaid
flowchart TD
    Client[Cliente Web / Script Python] <-->|HTTP via TCP/IP| Nios[Processador Nios II]
    
    subgraph FPGA Cyclone IV
        Nios <-->|Avalon-MM| HW[Acelerador Hardware VHDL]
        Nios <-->|Controlador SD| SD[(SD Card / Banco de Dados)]
    end
    
    HW -->|Processa Filtros e Agregações| HW
```

## 2. Diagrama de Comunicação (Rede e Software)

Como ocorre a comunicação e tradução das Queries no firmware em C.

```mermaid
sequenceDiagram
    participant Web as Cliente
    participant Nios as Firmware Nios II (C)
    participant SD as SD Card (.tbl)
    participant HW as Hardware (VHDL)
    
    Web->>Nios: HTTP POST /query (SQL)
    Nios->>Nios: Analisa Sintaxe SQL
    Nios->>SD: Busca Schema (Header do arquivo .tbl)
    SD-->>Nios: Nomes das Colunas e Metadados
    
    Nios->>HW: Envia Instruções (WHERE, COUNT, SUM) via IOWR
    Nios->>SD: Inicia leitura dos dados da Tabela
    SD-->>Nios: Blocos de Bytes
    Nios->>HW: Alimenta Pipeline VHDL com dados (IOWR)
    HW-->>Nios: Retorna dados que passaram no filtro (IORD)
    Nios-->>Web: HTTP 200 OK (Dados processados)
```

## 3. Arquitetura do Pipeline VHDL (Hardware)

Este esquema representa os blocos lógicos construídos na FPGA para acelerar a busca no banco de dados.

```mermaid
flowchart LR
    subgraph user_hw [Pipeline user_hw.v]
        direction LR
        InFIFO[FIFO de Entrada] --> Where[Filtro WHERE]
        Where --> Limit[Filtro LIMIT]
        Limit --> Agg[Funções Agregadas<br>SUM / COUNT]
        Agg --> OutFIFO[FIFO de Saída]
    end
    
    Bus[Barramento Avalon-MM] -->|Dados e Instruções| InFIFO
    OutFIFO -->|Dados Filtrados| Bus
    Agg -->|Resultados| Bus
```

## 4. Detalhes do Bloco `where_filter`

Detalhes de como o pipeline compara as informações de maneira encadeada.

```mermaid
flowchart TD
    DataIn[Dado da Coluna] --> Comp1{Comparador 1}
    DataIn --> Comp2{Comparador 2}
    DataIn --> Comp3{Comparador 3}
    DataIn --> Comp4{Comparador 4}
    
    Inst[Instrução WHERE] --> Comp1
    Inst --> Comp2
    Inst --> Comp3
    Inst --> Comp4
    
    Comp1 --> AND_Logic(Porta AND)
    Comp2 --> AND_Logic
    Comp3 --> AND_Logic
    Comp4 --> AND_Logic
    
    AND_Logic -->|Valid| DataOut[Permite Passagem da Linha]
```
