# FaunaSA - plano

ASI leve para o pacote de animais do Myths Maker 2.7 (Nadalao e Tarzan_3). Lê as pastas do mod original
(`cleo\Animals`, `cleo\Myths`) e substitui o runtime CLEO. O repositório só tem código; a release traz o pacote completo com os assets do mod original,
**com os créditos dos autores** (CREDITS.md). Sons e o script `.cs` originais não vão no pacote.

## Decisões (25/10/2026)
- Nome: **FaunaSA**.
- Animações que faltam: reaproveitar grupos existentes e testar; procedural só onde não servir.
- Criação de animais: ferramenta que gera a pasta + tecla para recarregar os `.ini`; sem menu no jogo por enquanto.

## Estado
Feito: leitura dos `.ini`, spawn com pool fixo, modelos por nome (Modloader `gta3.img`), carregador próprio de IFP ANP3,
IA v1 (vagar, fugir, perseguir, morder), correção de textura por modelo, vigia de travamento, registro de crash.

## Fases
1. **Paridade:** cavalo e interação; `recruitable`; locais por dia da semana e clima; sons (bass.dll); radar e mapa de vida
   selvagem; caçador `Anim0`; respawn e limites.
2. **Animações dos 8 tipos sem `.ifp`** (vaca `Anim4/5`, avestruz `Anim6`, ovelha `Anim7`, pato `Anim8`, gorila `Anim9`,
   capivara `Anim10`, raposa `Anim11/12`, galinha `Anim14`): conferir compatibilidade de esqueleto, mapear espécie -> grupo
   existente (arquivo de mapeamento editável), procedural para aves e gorila se preciso.
3. **Desempenho:** medir custo por frame do ASI e comparar com o original.
4. **Criar animais:** `tools/new_animal.py` + tecla de recarregar `.ini` + guia.
5. **README e lançamento:** padrão `readme-padrao`, créditos, requisitos, aviso de assets, renomear para FaunaSA (feito: `FaunaSA.asi`, `FaunaSA.log`).

## Referências
- https://www.mixmods.com.br/2022/10/myths-maker-animals-pack-mod-de-animais/
- https://libertycity.net/files/gta-san-andreas/182061-myths_maker-2.7-animal-world.html
- https://www.mixmods.com.br/2022/10/myths-maker-criar-mitos-lendas/

## Créditos do mod original
nadalao (autor), Tarzan_3 (animais e animações), Junior_Djjr (texturas, plugin ExtraDir e vozes), cavalo: nadalao, Junior_Djjr,
Pequeno, Retrkill0; cachorro animado: Infinity Ward, mcmillan, Dayk; skins de vários autores.
Termos do libertycity: uso em outros projetos com crédito; não alterar splash, logo e descrição do original.
