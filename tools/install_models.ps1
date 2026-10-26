<#
.SYNOPSIS
  Cria modloader\Myths Maker\gta3.img com os .dff/.txd de cleo\Animals e cleo\Myths, para o jogo registrar os modelos.
.DESCRIPTION
  Usa hardlinks (nao ocupa espaco extra) e cai para copia se a pasta estiver em outro volume. Nao apaga nada.
.EXAMPLE
  .\install_models.ps1 -Jogo "D:\Games\GTA San Andreas"
#>
param(
    [Parameter(Mandatory)][string]$Jogo,
    [string]$Destino
)
$ErrorActionPreference = 'Stop'
if (-not $Destino) { $Destino = Join-Path $Jogo 'modloader\Myths Maker\gta3.img' }
New-Item -ItemType Directory -Force $Destino | Out-Null
$novos = 0; $copias = 0
foreach ($sub in 'Animals', 'Myths') {
    $origem = Join-Path $Jogo "cleo\$sub"
    if (-not (Test-Path $origem)) { Write-Warning "nao achei $origem"; continue }
    $arquivos = Get-ChildItem $origem -Recurse -File | Where-Object { $_.Extension -in '.dff', '.txd' }
    foreach ($f in $arquivos) {
        $alvo = Join-Path $Destino $f.Name
        if (Test-Path $alvo) { continue }
        try { New-Item -ItemType HardLink -Path $alvo -Target $f.FullName | Out-Null }
        catch { Copy-Item $f.FullName $alvo; $copias++ }
        $novos++
    }
}
"{0} arquivos novos em {1} ({2} copiados, o resto hardlinks)" -f $novos, $Destino, $copias
