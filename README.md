# Alpha3Upper Public

Host nativo experimental para Windows x64, com importação local a partir de uma
instalação original compatível de **Capcom Fighting Collection 2**.

**Nenhum arquivo do jogo acompanha este repositório ou o pacote público.**
Não há ROM, código Capcom compilado, captura de memória, áudio, gráfico, shader
original, logo do jogo ou download desses materiais.

## Primeira execução

1. Baixe e extraia o pacote Windows da página **Releases**.
2. Abra `Alpha3Upper.exe` e escolha a pasta da sua coletânea original.
3. Abra a coletânea normalmente pela Steam, entre no **Alpha 3 Upper** e deixe
   uma partida ou treino aberto.
4. No assistente, clique em **Importar da instalação**. Ele confere a versão,
   lê o código carregado do original, extrai os recursos necessários e executa
   um teste local antes de concluir.
5. Depois da importação bem-sucedida, feche o original e clique em **Jogar**.

O pacote Windows não exige Python instalado. A importação não usa a rede,
não altera a instalação original e abre o processo somente para consulta e
leitura. Não suspende, injeta código nem escreve no processo original.

Os dados ficam em `%LOCALAPPDATA%\Alpha3UpperPublic\runtime-v1\data`, fora
da pasta do programa e do repositório. **Não envie essa pasta ao GitHub.**
As próximas execuções verificam os recursos locais. Uma atualização da
coletânea pode exigir suporte novo; versões desconhecidas são recusadas.

## Recursos do host

- Arcade, versus e treino; regeneração, dummy, hitboxes e medidor de quadros.
- Teclado e XInput configuráveis para dois jogadores e atalhos de golpes.
- Áudio com volumes separados, tela cheia, 4:3 / wide e filtros CRT.
- Online direto por IP ou por convite P2P, com rollback de até oito quadros.
- Lobby único público, apelidos, chat e convites aceitos/recusados.
- Os convites individuais continuam disponíveis sem entrar no lobby.

**Tela → Filtro CRT → CRT tipo B (aproximação)** é uma implementação própria
inspirada em uma apresentação de tubo com tríades RGB e linhas. Não incorpora
o shader da coletânea e ainda requer comparação visual lado a lado.

**Online → Lobby e chat** entra no lobby da versão pública. Use o mesmo
pacote nos dois PCs. A fila inicial no lobby é de um quadro; no convite
individual pode ser ajustada de zero a oito. Após aceitar, o lobby desconecta
e a luta segue diretamente por WebRTC. Volte ao local e reabra o lobby para
uma nova sessão. O chat é público, sem histórico no servidor; apelidos não
são contas verificadas. Algumas redes bloqueiam P2P e podem exigir VPN.

## Compatibilidade e limites

Esta primeira versão aceita uma revisão específica do executável Windows
e dos arquivos ARC, identificada por SHA-256 em `launcher/import_game.py`.
Não fornece nem baixa executáveis alternativos ou arquivos de autenticação.

O programa executa rotinas x64 originais, importadas localmente, junto de um
frontend independente. **Não é uma descompilação completa, nem um emulador.**
É um projeto experimental, não oficial e sem afiliação com a Capcom. Pode
haver diferenças e falhas. Uma correção visual breve é possível com rollback;
os sons esperam confirmação para evitar repetição. Os indicadores internos
de rede/desempenho não medem o atraso físico total do controle ao monitor.

## Compilar

No Windows x64, instale Python 3.13+ e MSYS2 UCRT64 com GCC, CMake, Ninja e
OpenSSL. Depois, no PowerShell na raiz do projeto:

```powershell
python -m pip install pyinstaller==6.22.3
$env:MINGW_BIN='C:\msys64\ucrt64\bin'
python tools/build.py
python tools/audit_public.py
```

O script obtém a revisão fixa de libdatachannel e seus submódulos. Você pode
compilar o frontend sem possuir recursos do jogo; eles só são necessários na
importação local. `dist/` contém somente o iniciador, o host e avisos/licenças.
Para compilar apenas o host: `python tools/build.py --core-only`.

## Servidor e contribuições

`server/` contém o Worker de lobby e sinalização. Ele não transporta comandos
de luta nem distribui recursos do jogo. A rota da versão pública é separada
do lobby experimental anterior. Consulte [arquitetura](docs/ARCHITECTURE.md).

Envie apenas código, descrições de erros e diagnósticos sem dados originais.
Não anexe dumps, arquivos ARC/AFS, músicas, capturas de código ou sua pasta
`data/` em issues, PRs, releases ou commits. O teste de publicação verifica
essas exclusões; `.gitignore` sozinho não é uma garantia.

MIT para o código próprio; dependências mantêm suas licenças. Veja
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Nenhuma licença deste projeto
autoriza redistribuir materiais da Capcom.
