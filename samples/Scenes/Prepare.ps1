param([Parameter(Mandatory = $true)][string]$Cooker)
$ErrorActionPreference = 'Stop'
$cookerPath = (Resolve-Path -LiteralPath $Cooker).Path
$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $gameRoot 'out/cooked/scene-samples'
# 素材本体を変更せず、専用の生成先へ変換する。
& $cookerPath (Join-Path $gameRoot 'samples/Assets/Cube/Cube.gltf') (Join-Path $output 'Cube.cmesh') --id samples/scene-cube
if ($LASTEXITCODE -ne 0) { throw 'Cube cook failed' }
& $cookerPath (Join-Path $gameRoot 'samples/Assets/RiggedFigure/RiggedFigure.glb') (Join-Path $output 'Figure.cskin') --id samples/scene-character
if ($LASTEXITCODE -ne 0) { throw 'Character cook failed' }
Write-Output (Join-Path $PSScriptRoot 'Example.cscene')
