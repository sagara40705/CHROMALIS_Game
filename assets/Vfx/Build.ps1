param([Parameter(Mandatory=$true)][string]$Effekseer)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
# AI素材を用意できない環境でも、同じ透過PNGの制作経路を再現する。
$pulse=New-Object System.Drawing.Bitmap 64,64
try {
    for($y=0;$y -lt 64;$y++){for($x=0;$x -lt 64;$x++){
        $r=[Math]::Sqrt([Math]::Pow(($x-31.5)/30,2)+[Math]::Pow(($y-31.5)/30,2))
        $ring=[Math]::Exp(-[Math]::Pow(($r-0.6)*13,2))
        $core=[Math]::Exp(-$r*$r*22)*0.55
        $a=[int](255*[Math]::Min(1,$ring+$core))
        $pulse.SetPixel($x,$y,[System.Drawing.Color]::FromArgb($a,90,180,240))
    }}
    $pulse.Save((Join-Path $PSScriptRoot 'Pulse.png'),[System.Drawing.Imaging.ImageFormat]::Png)
} finally {$pulse.Dispose()}
$probe=New-Object System.Drawing.Bitmap 2,2
try {
    for($y=0;$y -lt 2;$y++){for($x=0;$x -lt 2;$x++){$probe.SetPixel($x,$y,[System.Drawing.Color]::FromArgb(128,128,96,160))}}
    $probe.Save((Join-Path $PSScriptRoot 'AlphaProbe.png'),[System.Drawing.Imaging.ImageFormat]::Png)
} finally {$probe.Dispose()}
foreach($name in @('Pulse','AlphaProbe')) {
    $source=Join-Path $PSScriptRoot ($name+'.efkproj')
    $output=Join-Path $PSScriptRoot ($name+'.efkefc')
    # ツールが失敗時に終了コード0を返しても、古い正常ファイルを成功と誤認しない。
    $candidate=Join-Path $PSScriptRoot ($name+'.'+[guid]::NewGuid().ToString('N')+'.efkefc')
    & $Effekseer -cui -in $source -o $candidate | Out-Null
    if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $candidate)){throw 'Effekseer export failed'}
    $bytes=[IO.File]::ReadAllBytes($candidate)
    if($bytes.Length -lt 16 -or [Text.Encoding]::ASCII.GetString($bytes,0,4) -ne 'EFKE'){throw 'Invalid Effekseer output'}
    if(Test-Path -LiteralPath $output){[IO.File]::Replace($candidate,$output,[NullString]::Value)}else{[IO.File]::Move($candidate,$output)}
}
Write-Output 'VFX tool export completed; verify rendering separately.'
