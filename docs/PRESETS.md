# Presets e setlists

Código em `Source/presets/`, compilado como a biblioteca `pedal_presets`
(usada pelo plugin e pelo teste `PresetTest`).

## Onde ficam os arquivos

| Sistema | Pasta |
|---|---|
| Mac | `~/Library/Application Support/PedalApp/` |
| Windows | `%APPDATA%\PedalApp\` |
| Linux | `~/.config/PedalApp/` |
| iPad | pasta Documentos do app (aparece no app Arquivos) |

Dentro dela:

- `Presets/` — um `.json` por preset. Dá para copiar presets entre o PC e o iPad
  só copiando esses arquivos.
- `Setlists/` — um `.json` por setlist.
- `IRs/` — IRs de caixa. Se o caminho salvo no preset não existir (preset vindo
  de outro computador), o app procura um arquivo com o mesmo nome aqui.

Os arquivos são gravados primeiro num temporário e depois trocados de lugar, então
fechar o app no meio de um salvamento não estraga o preset.

## Formato do preset

```json
{
  "format": "pedal-app-preset",
  "version": 1,
  "id": "4b1f…",
  "name": "Drop C pesado",
  "category": "Guitarra",
  "favourite": true,
  "params": { "inputGain": 2.0, "gateThreshold": -60.0, "drop": -2, "cleanBlend": 0.0, "outputGain": 0.0, "cabEnabled": 1 },
  "amp": { "source": "tone3000", "toneId": "123", "modelId": "456", "name": "Mesa Rectifier", "creator": "fulano" },
  "ir": "/Users/thales/Library/Application Support/PedalApp/IRs/cab.wav"
}
```

- `params` usa as unidades reais (dB, semitons), não 0..1. Parâmetro que falta
  volta ao padrão; parâmetro desconhecido é ignorado. Assim presets antigos
  continuam abrindo quando entrarem parâmetros novos.
- `amp` é o `ToneRef`: `"source": "file"` com `"path"`, `"source": "tone3000"`
  com `toneId`/`modelId`, ou `"source": "none"` (sem amp, o sinal passa limpo).
- `id` não muda quando o preset é renomeado; as setlists apontam para ele.

## Setlists

Lista ordenada de presets (o mesmo preset pode aparecer mais de uma vez, e cada
entrada pode ter uma nota, como o nome da música). Com uma setlist ativa, a lista
da tela, os botões próximo/anterior e o MIDI seguem a ordem da setlist. Sem setlist,
vale a biblioteca inteira em ordem alfabética.

"Salvar como" com uma setlist ativa coloca o preset novo logo depois da música atual.

## Troca sem espera no show

As capturas de todos os presets da setlist ativa (ou dos favoritos, sem setlist)
ficam montadas na memória (`ModelBank`, até 64 capturas). Trocar para um desses
presets só troca um ponteiro: no teste leva menos de 0,1 ms e o `AmpModel` faz
crossfade de um bloco. Depois de usar uma captura, o banco monta outra cópia em
segundo plano, a partir do texto que já está na memória, para ela poder ser usada
de novo sem disco nem rede.

Captura que não está pré-carregada carrega em segundo plano; se outro preset for
escolhido antes de terminar, vale o último escolhido.

## MIDI

Program Change `n` escolhe a entrada `n` da lista (0 = primeira). Por padrão escuta
todos os canais; `PresetManager::setMidiChannel (1..16)` limita a um canal. A
thread de áudio só anota o pedido; a troca acontece na thread da interface (em até
~33 ms).

## Ligação com as outras partes

- **Interface:** `PresetManager` implementa `pedal::ui::PresetSource`
  (`Source/ui/PresetSource.h`). Também tem `next()`, `previous()`,
  `setActiveSetlist()`, `getPreloadStatus()` para o modo palco.
- **TONE3000:** `setToneFetcher()` recebe a função que transforma um `ToneRef` do
  TONE3000 no JSON do modelo (roda em thread de fundo).
  `setOfflineCacheHandler()` recebe a lista de `modelId`s usados em presets, para
  o cache offline guardar só esses.
- **Estado do plugin:** o projeto do DAW guarda preset atual, setlist, captura, IR e
  canal MIDI. Estados antigos (só `ampModel`/`cabinet`) continuam abrindo.
