# Esqueletos e animações: quais animais reaproveitam qual grupo

## Método
Todos os modelos do pacote usam o esqueleto padrão de ped do GTA SA (33 frames, 32 bones com HAnim). O que muda é a
**pose de repouso**: no SA as rotações da animação substituem as do osso, então uma animação só fica boa num modelo
cuja pose de repouso (inversa das matrizes do skin) seja parecida com a pose em que ela foi feita.

`tools/compare_skeletons.py` compara, para cada modelo, a pose de repouso real com o primeiro keyframe da animação
`*_idle` de cada grupo (rotações relativas ao Pelvis, em graus, média dos 32 bones; menor = mais compatível).
Calibração: os próprios modelos dos grupos ficam entre 8° e 15° do seu grupo (coelho 8,1°, leão 8,4°, lobo 8,3°, cervo 12,6°,
urso 13,2°, javali 14,9°, gato 14,5°; o cão dá 11,4° com o grupo do lobo e 21° com o dele).

## Resultado (graus)
| Animal (pasta) | Boar | rabit | deer1 | lion | wolf | bear | dog | cat | Decisão |
|---|---|---|---|---|---|---|---|---|---|
| ovelha (`Anim7`) | 18,6 | 44,0 | 28,3 | 26,6 | 24,9 | 16,5 | 32,5 | 17,8 | **Boar** (padrão) |
| avestruz (`Anim6`) | 25,5 | 49,2 | 20,4 | 19,2 | 16,8 | 24,9 | 26,3 | 26,4 | **deer1** (padrão; wolf 16,8 e lion 19,2 são alternativas) |
| vaca (`Anim4/5`) | 58,9 | 49,8 | 61,5 | 61,1 | 59,7 | 56,8 | 66,2 | 55,9 | nenhum serve |
| gorila (`Anim9`) | 58,9 | 49,8 | 61,5 | 61,1 | 59,7 | 56,9 | 66,2 | 55,9 | nenhum serve |
| raposa (`Anim11/12`) | 58,9 | 49,8 | 61,5 | 61,1 | 59,7 | 56,9 | 66,2 | 55,9 | nenhum serve |
| capivara (`Anim10`) | 62,5 | 53,1 | 64,7 | 64,4 | 62,9 | 60,4 | 69,5 | 59,3 | nenhum serve |
| pato (`Anim8`) | 112,4 | 113,3 | 111,7 | 111,7 | 111,6 | 112,3 | 112,0 | 112,5 | nenhum serve |
| galinha (`Anim14`) | 112,4 | 113,2 | 112,9 | 113,0 | 112,8 | 112,3 | 113,2 | 112,4 | nenhum serve |

## Conclusões
- **Só ovelha e avestruz** têm um grupo existente razoável. Os demais seis tipos não passam de ~50° de diferença com nenhum.
- Vaca, gorila, raposa e capivara têm **exatamente a mesma pose de repouso** (esqueleto padrão de humano, sem nenhum osso
  reposicionado). Eles foram feitos para usar as animações de humano do jogo, como o pacote original faz (`animgroup=0/4/11`).
  Para ficarem bons como quadrúpedes, precisam de animações próprias (feitas à mão ou geradas por código a partir da pose).
- Pato e galinha têm uma pose de repouso diferente de todas (aves bípedes); também precisam de animações próprias.
- Os valores são uma medida de distorção na pose parada, não de qualidade do ciclo de andar. Teste no jogo e ajuste o
  `FaunaSA.ini`.

## Próximos passos
1. Modo "animação de humano do jogo" com movimento da IA para os seis tipos sem grupo (como no pacote original).
2. Animações procedurais (andar e parado) a partir da pose de repouso para vaca, raposa, capivara e gorila; aves à parte.

## IFPs gerados (`tools/gen_ifp.py`)
Os seis tipos sem grupo existente têm a malha de quadrúpede/ave, mas só três ossos movem algo: o corpo (Pelvis/Spine) e as
pernas, presas às canelas `42` (par da frente) e `52` (par de trás), ou aos pés `43`/`53` nas aves. O gerador lê o próprio `.dff`
(rotações e posições de repouso dos frames, cuja cinemática bate com as matrizes inversas do skin) e escreve `walk`, `run`,
`sprint` e `idle` girando coxa e canela em volta do eixo lateral, com balanço do corpo. A velocidade vem de `4·A·L/T` (sem
deslizar o pé). `--preview` desenha a malha animada em PNG para conferir. Variantes com esqueleto diferente (galinha A14_3/4)
ganham grupo próprio `Fauna<modelo>`.

**Quatro patas.** No original as quatro patas de vaca, raposa e capivara estão presas a só dois ossos, então cada par andava junto
(um "pulinho"). O gerador também copia o `.dff` reatribuindo cada pata a um osso próprio (UpperArm L/R e Thigh L/R, que não tinham
malha) com o pivô no quadril/ombro, e a animação usa passo lateral (andar) e trote (correr). O gorila fica com o par, mais lento.
**Forma e chão:** a malha é deitada no eixo X; a animação do `Root` a gira para a frente do ped (igual ao `Boar.ifp`) e desce o
corpo até o chão. Aves têm escala ~3,3 no pelvis, que o IFP descarta: aparecem 3,3x menores que a malha.
