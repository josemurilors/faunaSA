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

## Packs de animais extras (0.3.0)
| Pacote | Autor | O que veio |
| --- | --- | --- |
| [Pack de animais](https://www.mixmods.com.br/2014/03/pack-de-animais/) (mixmods, 2014) | **kh gamer** (autor); post de Junior_Djjr | leão, tigre, porco, flamingo, lobo, urso preto e um cão (`WMYVA2`) |
| [Dog Pack](https://libertycity.net/files/gta-san-andreas/98286-dog-pack.html) (libertycity.net) | **Vadim Panenko** (pack); **StrideR** e **Ganic** (modelos); **KOT33** (skins) | cães grandes (Boxer, Dálmata, Doberman, Pastor, Bull, Malamute), cão marrom |
| [Cat Pack](https://libertycity.net/files/gta-san-andreas/91485-cat-pack.html) (libertycity.net) | **Vadim Panenko** | gato em 6 cores |

Nomes que aparecem dentro das texturas e modelos: JoaoPSoares (cão marrom `dwmolc`/`dwmylc`), mcmillan (pastor-alemão), GTAMonsterModz e StrideR.

**Termos:** a página do Pack de animais não traz termos de uso. A do Dog Pack diz que os cães podem ser usados "para qualquer fim" e a do Cat Pack não traz licença; as duas avisam que copiar sem permissão é proibido no site. Por isso estes arquivos vão só no pacote da release, com crédito, e serão removidos a pedido de qualquer autor (abra uma issue).

**Alterações feitas pelo FaunaSA nesses modelos:** os `.dff` de cães e gatos tiveram os triângulos reduzidos de ~60 mil para 10 mil (`tools/decimate_dff.py`, sem alterar UV nem pesos de skin dos vértices mantidos); as texturas são atlas 256×256 gerados com o [Texture Optimizer do gtastuff.com](https://gtastuff.com/tools/texture-optimizer/); os modelos de quatro patas ganham ossos de pata e animações geradas (`tools/gen_ifp.py`). Obra derivada dos autores acima.

## O que é do FaunaSA e o que não é
- **Do FaunaSA (MIT):** o plugin `FaunaSA.asi` (código em `src/`), `tools/gen_ifp.py`, `tools/import_pack.py`, `tools/decimate_dff.py` e os scripts de instalação.
- **Gerado pelo FaunaSA a partir dos modelos originais:** as animações `Fauna*.ifp` e os `.dff` de vaca, raposa e capivara com as quatro patas em ossos próprios (mesma malha e texturas do original, só os pesos do skin foram reatribuídos). Continuam sendo obra derivada dos autores acima.
- **Do original, sem alteração:** todos os demais modelos, texturas, `.ifp`, `.ini` e `animgrp.dat` do pacote. Os sons `.mp3` dos animais vão no pacote (o FaunaSA os toca); o script `.cs` do Myths Maker **não** vai.
- `tests/fixtures/` traz cópias mínimas de arquivos do Myths Maker (`Anim37.ini`, `Boar.ifp`, `animgrp_boar.dat`) usadas só nos testes automáticos.

## Cavalo: New Horse Mod
O cavalo cavalgável do FaunaSA é uma adaptação, em ASI, do [Novo mod de cavalo by Nadalao](https://www.mixmods.com.br/2018/12/novo-mod-de-cavalo-by-nadalao/) (2018). O FaunaSA **reescreve a lógica** (locais, montar, andar, pular, sons) com base no script original; o modelo, a textura, as animações (`horse_animation.ifp`) e os sons são do mod original.

| Quem | O que fez |
| --- | --- |
| **nadalao** | Autor do mod (script recriado do zero) |
| **Junior_Djjr** | Correções no `.ifp` e no `.dff` do cavalo, nova textura do pano, correção da posição de morte, recorte dos áudios, testes e ajuda no script |
| Pequeno e Retrkill0 | Variações para o PedFuncs |

A página do mod não indica o autor do modelo original do cavalo nem traz termos de redistribuição; por isso os arquivos do cavalo **não** estão no repositório. Quem tiver o mod instala com `tools/install_horse.ps1`.

## Referência técnica
[plugin-sdk](https://github.com/DK22Pac/plugin-sdk) (DK22Pac e comunidade), [gta-reversed](https://github.com/gta-reversed/gta-reversed) e o código aberto do [CLEO+](https://github.com/JuniorDjjr/CLEOPlus). O formato dos arquivos e as regras de spawn vêm da engenharia reversa do mod, para interoperar com ele.

## Aviso
Alguns modelos do pacote original (como o cachorro animado) vêm de terceiros com termos próprios. Quem for autor de qualquer item e quiser que seja removido ou creditado de outra forma, abra uma issue em <https://github.com/josemurilors/faunaSA/issues> e eu atendo.
