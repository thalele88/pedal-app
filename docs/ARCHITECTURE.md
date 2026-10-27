# Arquitetura e próximos passos

## Regra de ouro: a thread de áudio nunca espera

Tudo o que roda em `processBlock` (e em `pedal::SignalChain::process`) não pode
alocar memória, abrir arquivo, acessar rede nem travar mutex. Qualquer coisa
pesada (baixar captura, ler arquivo, montar modelo) roda em outra thread e entra
no áudio por troca atômica, como `pedal::AmpModel` já faz.

## Organização do código

| Pasta | O que tem |
|---|---|
| `Source/dsp/` | Motor de som, sem interface. `SignalChain` monta a cadeia; `AmpModel` guarda e troca a captura NAM; `DropTuner` faz o pitch shift. Compilado como a biblioteca `pedal_dsp`, usada pelo plugin e pelos testes. |
| `Source/ui/` | Telas: `PresetBrowser` (lista à esquerda), `SignalChainView` + `BlockPanels` (cadeia clicável), `StageView` (modo palco), `TunerView` (afinador), `PedalLookAndFeel` (paleta). A UI só fala com os presets pela interface `PresetSource`. |
| `Source/PluginProcessor.*` | Parâmetros (APVTS), estado salvo, ligação entre parâmetros e `SignalChain`. |
| `Source/PluginEditor.*` | Tela principal. |
| `Tests/` | Teste do motor sem interface (`EngineSmokeTest`). |

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

Feito na primeira versão: tudo da lista abaixo. A UI lê e troca presets só
por `pedal::ui::PresetSource` (`Source/ui/PresetSource.h`); hoje o processor
devolve `SessionPresets`, uma lista só em memória, e o `PresetManager` da frente 2
passa a ser devolvido por `PedalAudioProcessor::getPresetSource()`.
O afinador lê a entrada por `pedal::TunerInput` (FIFO sem lock, só ativo com a
tela aberta) e detecta a nota fora da thread de áudio.

- Navegador de presets à esquerda (ideia do ToneX).
- Cadeia de blocos clicáveis, cada bloco abre seus controles (ideia da Neural DSP).
- Modo palco: tela cheia, 4 a 8 botões gigantes, nome do preset grande,
  afinação a um toque, alto contraste. Pensado para toque no iPad.
- Afinador cromático.

### 4. Qualidade e latência do drop (`Source/dsp/DropTuner.*`, branch `feat/drop-tuner`)

- Hoje: Signalsmith Stretch com janela de 30 ms (≈30 ms de latência quando ligado).
- Medir com guitarra e baixo reais; testar janelas menores e alternativas de
  baixa latência; talvez um modo "qualidade" e outro "latência mínima".

### 5. iPad (depois das frentes 1 a 3)

- Adicionar os formatos `AUv3` e standalone iOS no `CMakeLists.txt` e gerar pelo
  Xcode. Testar horas seguidas sem travar.
