# Créditos

O FaunaSA só existe por causa do trabalho de outras pessoas. Este arquivo vale para o repositório e para o pacote completo da release.

## Mod original: Myths Maker 2.7 / Animals Pack
| Quem | O que fez |
| --- | --- |
| **nadalao** | Autor do Myths Maker 2.7 (script, formato dos `.ini`, regras de spawn) |
| **Tarzan_3** | Os animais (modelos) e as animações dos animais (`.ifp`) |
| **Junior_Djjr** | Texturas, plugin `SimpleLimitAdjuster_ExtraDir`, CLEO+ e vozes de IA |
| Pequeno, Retrkill0 | Cavalo (junto com nadalao e Junior_Djjr) |
| Infinity Ward, mcmillan, Dayk | Cachorro animado |
| vários autores | Skins, listados nas páginas abaixo |

Páginas do mod original (termos do autor: uso em outros projetos **com crédito**, sem alterar splash, logo e descrição do mod original):

- [Myths Maker 2.7 (libertycity.net)](https://libertycity.net/files/gta-san-andreas/182061-myths_maker-2.7-animal-world.html)
- [Myths Maker – Animals Pack (mixmods)](https://www.mixmods.com.br/2022/10/myths-maker-animals-pack-mod-de-animais/)
- [Myths Maker: criar mitos e lendas (mixmods)](https://www.mixmods.com.br/2022/10/myths-maker-criar-mitos-lendas/)

## O que é do FaunaSA e o que não é
- **Do FaunaSA (MIT):** o plugin `FaunaSA.asi` (código em `src/`), `tools/gen_ifp.py` e os scripts de instalação.
- **Gerado pelo FaunaSA a partir dos modelos originais:** as animações `Fauna*.ifp` e os `.dff` de vaca, raposa e capivara com as quatro patas em ossos próprios (mesma malha e texturas do original, só os pesos do skin foram reatribuídos). Continuam sendo obra derivada dos autores acima.
- **Do original, sem alteração:** todos os demais modelos, texturas, `.ifp`, `.ini` e `animgrp.dat` do pacote. Os sons e o script `.cs` do Myths Maker **não** vão no pacote (o FaunaSA não os usa).
- `tests/fixtures/` traz cópias mínimas de arquivos do Myths Maker (`Anim37.ini`, `Boar.ifp`, `animgrp_boar.dat`) usadas só nos testes automáticos.

## Referência técnica
[plugin-sdk](https://github.com/DK22Pac/plugin-sdk) (DK22Pac e comunidade), [gta-reversed](https://github.com/gta-reversed/gta-reversed) e o código aberto do [CLEO+](https://github.com/JuniorDjjr/CLEOPlus). O formato dos arquivos e as regras de spawn vêm da engenharia reversa do mod, para interoperar com ele.

## Aviso
Alguns modelos do pacote original (como o cachorro animado) vêm de terceiros com termos próprios. Quem for autor de qualquer item e quiser que seja removido ou creditado de outra forma, abra uma issue em <https://github.com/josemurilors/faunaSA/issues> e eu atendo.
