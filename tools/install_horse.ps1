<#
.SYNOPSIS
  Instala os arquivos do "New Horse Mod" (nadalao) no jogo para o cavalo do FaunaSA. Nao instala o script cavalo.cs.
.DESCRIPTION
  Copia cavalo.dff/.txd para modloader\FaunaSA\gta3.img, e horse_animation.ifp + os 5 .mp3 para FaunaSA\horse.
.EXAMPLE
  .\install_horse.ps1 -Jogo "D:\Games\GTA San Andreas" -Cavalo "C:\...\New Horse Mod (cavalo)"
#>
param([Parameter(Mandatory)][string]$Jogo, [Parameter(Mandatory)][string]$Cavalo)
$ErrorActionPreference = 'Stop'
$img = Join-Path $Jogo 'modloader\FaunaSA\gta3.img'
$dados = Join-Path $Jogo 'FaunaSA\horse'
New-Item -ItemType Directory -Force $img, $dados | Out-Null
Copy-Item -LiteralPath (Join-Path $Cavalo 'cavalo.dff'), (Join-Path $Cavalo 'cavalo.txd') $img -Force
Copy-Item -LiteralPath (Join-Path $Cavalo 'horse_animation.ifp') $dados -Force
Copy-Item -LiteralPath (Get-ChildItem (Join-Path $Cavalo 'cleo\cleo_audio\*.mp3')).FullName $dados -Force
"cavalo instalado em $img e $dados"
