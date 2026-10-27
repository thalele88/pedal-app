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
| `Source/tone3000/` | Conexão com o TONE3000: login OAuth/PKCE, `select_tone`, download de modelos, cache offline e limite de pedidos. Compilado como `pedal_tone3000`. Nunca roda na thread de áudio. |
| `Source/ui/` | Visual compartilhado (`PedalLookAndFeel`, paleta de cores). |
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
- **Feito** (ver `Source/tone3000/Tone3000Service.h`). O que os outros módulos usam:

```cpp
pedal::tone3000::Service& t3k = processor.getTone3000();

// Thread de fundo. Lê a cópia em disco se existir; senão baixa. Com
// keepOfflineFor ("presets") a cópia fica no disco para esse dono.
juce::String error = t3k.fetchModelJson (toneId, modelId, namJson, "presets");

// Sempre que o banco de presets mudar: a lista de modelos que os presets usam.
// Tudo que nenhum dono ("presets", "current") usa é apagado do disco.
t3k.retainOffline ("presets", modelIdsDosPresets);
```

  O processador usa o dono `"current"` para a captura que está no amp (é o que o
  estado do plugin restaura). Depois do JSON, o carregamento continua sendo
  `AmpModel::loadFromJson` fora da thread de áudio.
- Contrato com os presets (o `ToneRef` mora em `Source/presets/ToneRef.h`):

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

- Hoje: Signalsmith Stretch com janela de 30 ms (≈30 ms de latência quando ligado).
- Medir com guitarra e baixo reais; testar janelas menores e alternativas de
  baixa latência; talvez um modo "qualidade" e outro "latência mínima".

### 5. iPad (depois das frentes 1 a 3)

- Adicionar os formatos `AUv3` e standalone iOS no `CMakeLists.txt` e gerar pelo
  Xcode. Testar horas seguidas sem travar.
