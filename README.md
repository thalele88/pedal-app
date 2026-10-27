# Pedal App

Plugin e app standalone de amp/pedal para guitarra e baixo, usando capturas
[NAM](https://github.com/sdatkinson/neural-amp-modeler) (incluindo as do
[TONE3000](https://www.tone3000.com)), com drop de afinação embutido.
Primeiro roda no PC (Windows/Mac); depois o mesmo código vira app de iPad para
tocar ao vivo.

![Tela atual](docs/screenshot.png)

## O que já funciona

- Carregar uma captura `.nam` do computador (A1 e A2) e um IR de caixa `.wav`.
- Cadeia: entrada → gate → afinação → amp NAM → caixa → mix limpo → saída.
- Drop de afinação polifônico de +12 a −12 semitons (botões de atalho de 0 a −12).
- Troca de captura sem estalo: a nova entra com crossfade e nada é carregado na
  thread de áudio.
- **Modo de teste sem interface:** toca uma gravação limpa (DI) de guitarra ou
  baixo em loop pela cadeia, para ouvir capturas e o drop pelo fone ou pela
  caixa do computador.
- Medidor de CPU e aviso quando a captura foi treinada em outra taxa de amostragem.
- Botão **TONE3000**: escolhe um tom no catálogo do TONE3000 e toca direto, sem
  baixar arquivo à mão.
- Formatos: Standalone e VST3 (Windows/Mac/Linux) e AU (Mac).

## Baixar pronto (sem compilar)

Cada PR e cada commit na `main` gera o app pronto para Windows, Mac e Linux:

1. Abra a aba **Actions** do repositório (ou, no PR, clique em **Checks**) e
   entre na execução mais recente do **Build** com ✓ verde.
2. No fim da página, em **Artifacts**, baixe **PedalApp-Windows** (precisa estar
   logado no GitHub).
3. Descompacte o `.zip`:
   - `Standalone/Pedal App.exe`: abre direto. Se o Windows mostrar "O Windows
     protegeu o computador", clique em **Mais informações → Executar assim mesmo**
     (o app não é assinado).
   - `VST3/Pedal App.vst3`: copie a pasta para `C:\Program Files\Common Files\VST3`
     para usar num DAW.

Os arquivos ficam disponíveis por 30 dias em cada execução.

## Como compilar

Precisa de **CMake 3.24+** e de um compilador C++20:

- **Mac:** Xcode (pelo App Store) e `brew install cmake`
- **Windows:** Visual Studio 2022 com "Desenvolvimento para desktop com C++"

```bash
git clone https://github.com/thalele88/pedal-app.git
cd pedal-app
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

A primeira configuração baixa o JUCE, o NAM core e o Signalsmith Stretch, então
demora alguns minutos.

O resultado fica em `build/PedalApp_artefacts/Release/`:

- `Standalone/Pedal App` (`.app` no Mac, `.exe` no Windows) para abrir direto
- `VST3/Pedal App.vst3` para usar num DAW

No Mac, para gerar um projeto do Xcode: `cmake -S . -B build-xcode -G Xcode`.

### TONE3000

O app usa a API oficial do TONE3000 com login OAuth. A chave pública do app
(`client_id`, a "publishable key" da conta de desenvolvedor) já está no
`CMakeLists.txt`, então o build normal já vem com o botão TONE3000 funcionando.

- A chave pública pode ficar no código: ela vai dentro do app de qualquer jeito.
  A **secret key** (`t3k_cs_...`) nunca deve entrar no repositório nem no app.
- Não precisa cadastrar redirect URI: o login no PC volta para
  `http://127.0.0.1:<porta>/callback`, que o TONE3000 aceita sempre.
- Para usar outra chave: `-DPEDAL_TONE3000_CLIENT_ID=...` no `cmake` ou a
  variável de ambiente `TONE3000_CLIENT_ID`.

O plano grátis da API é para projetos não comerciais e permite o fluxo
`select_tone` que o app usa. O limite é 100 pedidos por minuto; o app fica em
90 sozinho.

### Teste rápido do motor

```bash
ctest --test-dir build -C Release --output-on-failure
```

Ele processa 10 segundos de áudio com as capturas de exemplo do NAM, troca de
captura no meio e confere que não sai NaN, que não fica mudo e que roda bem mais
rápido que tempo real. O `Tone3000Test` confere as partes do TONE3000 que não
precisam de internet (PKCE, retorno do login, limite de pedidos e cache).

## Usando no standalone

1. Em **Options → Audio/MIDI Settings**, escolha sua interface de áudio e um
   buffer de 64 ou 128 amostras.
2. O JUCE silencia a entrada na primeira vez para evitar microfonia: desmarque
   "Mute audio input" na mesma tela.
3. Clique em **AMP** para escolher um `.nam` e em **CAIXA** para escolher um IR.
4. Ou clique em **TONE3000**: a tela do TONE3000 abre dentro do próprio app
   (como no editor do M-VAVE MK300). Na primeira vez aparece uma explicação e
   você entra na sua conta ali mesmo. Na aba **Catálogo** fica o site do
   TONE3000, com voltar, avançar e recarregar; nas abas **Favoritos**, **Em
   alta** e **Novidades** ficam as listas. Clique num tom, escolha a captura
   (uma por canal/ajuste) e ela entra no AMP. Só aparecem capturas **NAM A2**.
   Embaixo do nome aparece "Powered by TONE3000 · criador", como o TONE3000
   pede. O login fica salvo (Keychain no Mac, protegido pelo Windows no PC).
   No Windows o site roda no WebView2 da Microsoft, que já vem no Windows 11 e
   em quase todo Windows 10; sem ele, o app oferece abrir o catálogo no
   navegador.

### Sem internet no show

O TONE3000 não deixa guardar o catálogo, então o app só guarda no disco as
capturas que estão em uso: a captura atual do amp (para a sessão abrir offline)
e, quando os presets chegarem, as capturas dos presets. Capturas que nenhum
preset usa mais são apagadas na hora. A pasta é
`<dados do app>/Pedal App/TONE3000/models`.

## Testando sem interface de áudio

1. Arrume uma gravação **DI** (guitarra ou baixo gravado limpo, direto na linha,
   sem amp nem efeito) em `.wav`, `.aif` ou `.flac`. Muitos sites de captura e
   fóruns de NAM oferecem DIs grátis para teste; qualquer faixa DI de multitrack
   também serve.
2. Abra o standalone, clique em **Arquivo DI...** e escolha o arquivo. Ele
   começa a tocar em loop; **Tocar DI / Parar DI** liga e desliga.
3. Em **Options → Audio/MIDI Settings**, deixe a saída nos fones ou alto-falantes
   do computador. A entrada pode ficar em qualquer coisa: com o DI tocando, ela
   é ignorada.
4. Carregue uma captura em **AMP** e troque a afinação nos botões de baixo para
   ouvir o drop.

## Limitações conhecidas

- O drop de afinação adiciona **cerca de 30 ms** de latência quando está ligado
  (em "Padrão" não adiciona nada). É o ponto que mais precisa de teste com
  instrumento de verdade.
- Ainda não há presets nem modo palco. Veja o roteiro em
  [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
- O login do TONE3000 no iPad ainda não existe (no iOS o retorno do login é por
  URL scheme ou pelo fluxo de código de dispositivo; fica para a fase do iPad).
- No Linux (só usado no CI) a conexão com o TONE3000 não funciona, porque o
  JUCE está compilado sem curl.

## Licenças

- [JUCE](https://juce.com): licença gratuita para uso pessoal (ou AGPLv3).
- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore): MIT.
- [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch): MIT.
- Capturas do TONE3000 pertencem aos seus criadores; respeite a licença de cada uma.
