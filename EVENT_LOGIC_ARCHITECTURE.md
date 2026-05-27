# Arquitetura de Eventos e Geração de Código (IDE Embedded)

Este documento descreve a "Lei Imutável" do fluxo de compilação e execução de eventos na IDE Embedded. O motor de compilação (`CodeGenerator.cpp`) deve respeitar essa estrutura rigorosamente para garantir a estabilidade do código C++ gerado para microcontroladores (como o ESP32).

## O Conceito do "Sanduíche de Eventos"

A arquitetura orientada a eventos da IDE separa estritamente o código escrito/montado pelo usuário da lógica física de hardware (debouncing, leituras de pinos). O fluxo é dividido em três etapas claras:

### 1. Fora do Loop (Escopo Global)
Todas as funções que contêm a lógica do usuário e as rotinas de monitoramento de hardware devem ser declaradas no escopo global, **antes** ou fora das funções `setup()` e `loop()`.

#### A. A Função de Evento (Código do Usuário)
Quando o usuário arrasta blocos para um evento (ex: `aoClicar` de um botão), esse código é inserido dentro de uma função dedicada.
*   **Regra:** Essa função **NUNCA** faz leitura direta de hardware. Ela apenas executa a lógica de negócios.
*   **Nomenclatura:** `[NomeDoComponente]_event[NomeDoEvento]()`
*   **Exemplo:**
    ```cpp
    void Botao1_eventAoClicar() {
        // Código gerado a partir dos blocos do usuário
        digitalWrite(PIN_LED1, HIGH);
    }
    ```

#### B. A Função de Monitoramento (Lógica de Hardware)
Para cada evento físico, existe uma função "Monitor". É ela quem suja as mãos lidando com `digitalRead`, `analogRead`, *debounce*, histerese e controle de estado.
*   **Regra:** Se a condição física do evento for confirmada (ex: o botão foi realmente clicado, ignorando ruídos), o monitor **chama a Função de Evento** descrita acima.
*   **Nomenclatura:** `monitor_[NomeDoComponente]_[NomeDoEvento]()`
*   **Exemplo:**
    ```cpp
    void monitor_Botao1_eventAoClicar() {
        int reading_Botao1 = digitalRead(PIN_Botao1);
        // ... lógica de debounce ...
        if (clique_confirmado) {
            Botao1_eventAoClicar(); // Chama a lógica do usuário
        }
    }
    ```

### 2. A Função `setup()`
Utilizada apenas para inicialização.
*   Configuração dos pinos (`pinMode`).
*   Inicialização de bibliotecas (`Serial.begin`, `dht.begin()`).
*   Chamada única do evento global `aoIniciar()` do microcontrolador.

### 3. A Função `loop()` (O Maestro)
A função `loop()` deve permanecer o mais enxuta possível. Seu único trabalho é ser o maestro que chama os monitores em cada ciclo de clock.
*   **Regra:** O código gerado pelos blocos do usuário **NUNCA** é injetado diretamente no `loop()` (exceto para o evento específico `aoLoop()` do microcontrolador).
*   **Exemplo:**
    ```cpp
    void loop() {
        // O loop apenas invoca os monitores.
        monitor_Botao1_eventAoClicar();
        monitor_SensorTemp_eventAoCalcular();
        
        // E executa o evento de loop principal, se houver
        aoLoop(); 
    }
    ```

## Por que essa arquitetura não pode ser alterada?

1.  **Isolamento de Erros:** Se o usuário criar um *loop* infinito ou uma lógica muito pesada em seus blocos, isso fica confinado dentro da função `_eventAo...()`. Não "quebra" a sintaxe do `loop()` principal.
2.  **Organização:** O código C++ gerado se mantém legível para humanos. Desenvolvedores experientes podem exportar o código e entender claramente onde está a lógica física e onde está a lógica de negócios.
3.  **Padronização (Componentes Nativos vs Customizados):** O motor de compilação aplica essa mesma regra de "Monitor -> Evento" tanto para um Botão nativo quanto para um Sensor Customizado complexo importado via JSON.

**Aviso aos Desenvolvedores:** Qualquer modificação no arquivo `CodeGenerator.cpp` deve preservar este fluxo. Não coloque chamadas de blocos de usuários diretamente dentro de rotinas de monitoramento ou dentro do `loop()`. Mantenha a separação de responsabilidades.
