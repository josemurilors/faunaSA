<#
.SYNOPSIS
  Gera as animacoes (IFP) e os modelos com 4 patas dos animais sem .ifp e instala no jogo.
.DESCRIPTION
  1. roda tools\gen_ifp.py (le cleo\Animals, nao altera nada la);
  2. copia os .ifp e o animgrp.dat para cleo\Animals\FaunaSA;
  3. troca os .dff das patas em modloader\Myths Maker\gta3.img pelas versoes com 4 patas. Os arquivos de cleo\Animals sao
     hardlinks desses: a troca desfaz o link, entao os originais continuam intactos (para desfazer, apague o .dff em
     modloader\Myths Maker\gta3.img e rode install_models.ps1).
  Feche o jogo antes.
.EXAMPLE
  .\install_anims.ps1 -Jogo "D:\Games\GTA San Andreas"
#>
param([Parameter(Mandatory)][string]$Jogo)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path $PSScriptRoot
$saida = Join-Path $raiz 'out\anims'
if (Test-Path $saida) { Remove-Item $saida -Recurse -Force }
python (Join-Path $PSScriptRoot 'gen_ifp.py') (Join-Path $Jogo 'cleo\Animals') $saida
if ($LASTEXITCODE) { throw 'gen_ifp.py falhou' }
$fauna = Join-Path $Jogo 'cleo\Animals\FaunaSA'
New-Item -ItemType Directory -Force $fauna | Out-Null
Copy-Item "$saida\*.ifp", "$saida\animgrp.dat" $fauna -Force
$img = Join-Path $Jogo 'modloader\Myths Maker\gta3.img'
if (-not (Test-Path $img)) { throw "rode install_models.ps1 antes: nao achei $img" }
foreach ($f in Get-ChildItem "$saida\models\*.dff") {
    $alvo = Join-Path $img $f.Name
    if (Test-Path $alvo) { Remove-Item $alvo -Force }
    Copy-Item $f.FullName $alvo
}
"{0} animacoes e {1} modelos instalados" -f (Get-ChildItem "$saida\*.ifp").Count, (Get-ChildItem "$saida\models\*.dff").Count
