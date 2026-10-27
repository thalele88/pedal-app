# Arquitetura e próximos passos

## Regra de ouro: a thread de áudio nunca espera

Tudo o que roda em `processBlock` (e em `pedal::SignalChain::process`) não pode
alocar memória, abrir arquivo, acessar rede nem travar mutex. Qualquer coisa
pesada (baixar captura, ler arquivo, montar modelo) roda em outra thread e entra
no áudio por troca atômica, como `pedal::AmpModel` já faz.

## Organização do código

| Pasta | O que tem |
|---|---|
| `Source/dsp/` | Motor de som, sem interface. `SignalChain` monta a cadeia; `AmpModel` guarda e troca a captura NAM; `DropTuner` faz o pitch shift (usando `SpliceShifter` ou o Signalsmith Stretch). Compilado como a biblioteca `pedal_dsp`, usada pelo plugin e pelos testes. |
| `Source/ui/` | Visual compartilhado (`PedalLookAndFeel`, paleta de cores). |
| `Source/PluginProcessor.*` | Parâmetros (APVTS), estado salvo, ligação entre parâmetros e `SignalChain`. |
| `Source/PluginEditor.*` | Tela principal. |
| `Tests/` | Teste do motor sem interface (`EngineSmokeTest`) e `DropCompare`, que gera áudios dos dois modos do drop. |

## Frentes de trabalho que podem andar em paralelo

Cada frente trabalha numa branch própria a partir da `main` e abre seu PR.
As interfaces abaixo são o contrato entre elas.

### 1. Conexão com o TONE3000 (`Source/tone3000/`, branch `feat/tone3000`)

- Login OAuth 2.0 + PKCE com o `client_id` público (nunca chave secreta no app).
  No desktop, redirect para um servidor local em `127.0.0.1`; no iOS, URL scheme.
- Fluxo `select_tone` para escolher tom na página do TONE3000.
- Baixar o modelo por `GET /models?tone_id=` → `model_url` (com o token) e
  entregar o JSON para `AmpModel::loadFromJson (json, nome)`, tudo fora da thread
  de áudio.
- Guardar no disco só os modelos usados em presets (cache para tocar offline).
- Respeitar 100 req/min e mostrar "Powered by TONE3000" + nome do criador.
- Contrato com os presets:

```cpp
struct ToneRef
{
    enum class Source { localFile, tone3000 };
    Source source;
    juce::String localPath;          // quando localFile
    juce::String toneId, modelId;    // quando tone3000
    juce::String displayName, creator;
};
```

### 2. Presets e setlists (`Source/presets/`, branch `feat/presets`)

- Preset em JSON: valores dos parâmetros + `ToneRef` do amp + caminho do IR.
- Banco de presets em pasta do usuário, favoritos, setlists ordenadas.
- Troca por MIDI Program Change e por botões na tela.
- Pré-carregar todas as capturas da setlist em memória para trocar sem esperar
  (vai exigir vários slots no `AmpModel` ou um `AmpModel` por preset carregado).

### 3. Interface e modo palco (`Source/ui/`, branch `feat/stage-ui`)

- Navegador de presets à esquerda (ideia do ToneX).
- Cadeia de blocos clicáveis, cada bloco abre seus controles (ideia da Neural DSP).
- Modo palco: tela cheia, 4 a 8 botões gigantes, nome do preset grande,
  afinação a um toque, alto contraste. Pensado para toque no iPad.
- Afinador cromático.

### 4. Qualidade e latência do drop (`Source/dsp/DropTuner.*`, branch `feat/drop-tuner`)

- Dois motores, escolhidos pelo parâmetro `dropMode`:
  - **Rápido** (`SpliceShifter`, domínio do tempo): linha de atraso lida em outra
    velocidade; os saltos são escolhidos por correlação para cair num período
    da nota. Atraso médio de 7 a 10 ms em notas, 15 ms em acordes, 19 ms em
    baixo (varia o tempo todo; o host recebe 13 ms).
  - **Suave** (Signalsmith Stretch): janela de 60 ms com formato assimétrico
    (0.7), 18 ms fixos. O `CMakeLists.txt` gera uma cópia do header com o
    parâmetro de assimetria, que a API original não expõe.
- Trocar de modo com o drop ligado faz crossfade depois que o motor novo encheu.
- Próximo passo: ouvir com guitarra e baixo reais (`DropCompare`) e ajustar as
  constantes de `SpliceShifter.h` (limiares de correlação e tamanhos de salto).

### 5. iPad (depois das frentes 1 a 3)

- Adicionar os formatos `AUv3` e standalone iOS no `CMakeLists.txt` e gerar pelo
  Xcode. Testar horas seguidas sem travar.
