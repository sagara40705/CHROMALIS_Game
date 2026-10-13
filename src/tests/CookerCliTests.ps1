param([Parameter(Mandatory=$true)][string]$Cooker,
      [Parameter(Mandatory=$true)][string]$Source,
      [Parameter(Mandatory=$true)][string]$Root,
      [string]$EditorRunner)
$ErrorActionPreference = 'Stop'
$run = Join-Path $Root ([Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run) | Out-Null
function FileHash([string]$path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($path))) }
    finally { $sha.Dispose() }
}
function New-Case([string]$name) {
    $dir = Join-Path $run $name
    [IO.Directory]::CreateDirectory($dir) | Out-Null
    foreach ($file in @('Cube.gltf','Cube.bin','Cube_BaseColor.png')) {
        [IO.File]::Copy((Join-Path $Source $file),(Join-Path $dir $file))
    }
    [IO.File]::WriteAllText((Join-Path $dir 'result.cmesh'),'previous cooked bytes')
    [IO.File]::WriteAllText((Join-Path $dir 'result.cmesh.report.txt'),'stale Cook OK report')
    return $dir
}
function Snapshot([string]$dir) {
    $hashes = @{}
    Get-ChildItem -LiteralPath $dir -File | ForEach-Object { $hashes[$_.Name] = FileHash $_.FullName }
    return $hashes
}
function Unchanged([string]$dir, $before) {
    $after = Snapshot $dir
    if ($before.Count -ne $after.Count) { throw "Unexpected file creation in $dir" }
    foreach ($name in $before.Keys) { if ($after[$name] -ne $before[$name]) { throw "File changed: $dir/$name" } }
}
function Rejected([string]$dir, [string]$output, [string]$report, [string]$inputFile = 'Cube.gltf') {
    $before = Snapshot $dir
    $message = & $Cooker (Join-Path $dir $inputFile) (Join-Path $dir $output) --id tests/report --report (Join-Path $dir $report)
    if ($LASTEXITCODE -ne 1 -or "$message" -notmatch 'Cook FAILED') { throw "Expected exit 1 and fresh error in $dir : $message" }
    Unchanged $dir $before
}
try {
    foreach ($target in @('Cube.gltf','Cube.bin','Cube_BaseColor.png','result.cmesh')) {
        $dir = New-Case ('report-' + $target)
        Rejected $dir 'result.cmesh' $target
    }
    foreach ($target in @('Cube.gltf','Cube.bin','Cube_BaseColor.png')) {
        $dir = New-Case ('output-' + $target)
        Rejected $dir $target 'result.cmesh.report.txt'
    }
    $dir = New-Case 'case-alias'
    Rejected $dir 'new.cmesh' 'NEW.CMESH'
    $dir = New-Case 'hardlink'
    New-Item -ItemType HardLink -Path (Join-Path $dir 'alias.txt') -Target (Join-Path $dir 'Cube_BaseColor.png') | Out-Null
    Rejected $dir 'result.cmesh' 'alias.txt'
    foreach ($target in @('Cube.gltf','Cube.bin','Cube_BaseColor.png','result.cmesh','result.cmesh.report.txt','unverified-new.txt')) {
        $dir = New-Case ('parse-fail-' + $target)
        [IO.File]::WriteAllText((Join-Path $dir 'Cube.gltf'),'{ invalid')
        Rejected $dir 'result.cmesh' $target
    }
    $dir = New-Case 'dependency-fail'
    [IO.File]::WriteAllText((Join-Path $dir 'Cube_BaseColor.png'),'not a PNG')
    Rejected $dir 'result.cmesh' 'unverified-new.txt'
    $dir = New-Case 'missing-input'
    Rejected $dir 'result.cmesh' 'result.cmesh.report.txt' 'missing.gltf'
    $dir = New-Case 'report-directory'
    [IO.Directory]::CreateDirectory((Join-Path $dir 'directory.txt')) | Out-Null
    Rejected $dir 'result.cmesh' 'directory.txt'
    $dir = New-Case 'bad-arguments'
    $before = Snapshot $dir
    & $Cooker (Join-Path $dir 'Cube.gltf') (Join-Path $dir 'result.cmesh') --bad tests/report --report (Join-Path $dir 'Cube.gltf') | Out-Null
    if ($LASTEXITCODE -ne 1) { throw 'Invalid arguments accepted' }
    Unchanged $dir $before
    $dir = New-Case 'success'
    $before = Snapshot $dir
    & $Cooker (Join-Path $dir 'Cube.gltf') (Join-Path $dir 'result.cmesh') --id tests/report --report (Join-Path $dir 'result.cmesh.report.txt') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Valid cook failed' }
    foreach ($file in @('Cube.gltf','Cube.bin','Cube_BaseColor.png')) {
        if ((FileHash (Join-Path $dir $file)) -ne $before[$file]) { throw 'Successful cook changed source' }
    }
    if ([IO.File]::ReadAllText((Join-Path $dir 'result.cmesh.report.txt')) -notmatch '^Cook OK:') { throw 'Fresh report missing' }
    if ((FileHash (Join-Path $dir 'result.cmesh')) -eq $before['result.cmesh']) { throw 'Cooked output not published' }
    if ($EditorRunner) {
        $dir = New-Case 'editor-report-is-dependency'
        $report = Join-Path $dir 'result.cmesh.report.txt'
        [IO.File]::Copy((Join-Path $dir 'Cube_BaseColor.png'),$report,$true)
        $inputPath = Join-Path $dir 'Cube.gltf'
        [IO.File]::WriteAllText($inputPath,[IO.File]::ReadAllText($inputPath).Replace('Cube_BaseColor.png','result.cmesh.report.txt'))
        $hash = FileHash $report
        $message = & $EditorRunner $inputPath (Join-Path $dir 'result.cmesh')
        if ($LASTEXITCODE -ne 0 -or "$message" -notmatch 'Cook OK:' -or (FileHash $report) -ne $hash) { throw 'Editor overwrote report dependency or failed to capture success' }
        [IO.File]::WriteAllText($inputPath,'{ invalid')
        $before = Snapshot $dir
        $message = & $EditorRunner $inputPath (Join-Path $dir 'result.cmesh')
        if ($LASTEXITCODE -ne 1 -or "$message" -notmatch 'Cook FAILED:' -or "$message" -match 'Cook OK:') { throw 'Editor lost error / displayed stale success' }
        Unchanged $dir $before
    }
    Write-Output 'PASS: CLI collision/parse/dependency/argument failures preserve copies; exit codes and report success checked; optional Editor pipe checked'
    exit 0
} catch { Write-Error $_; exit 1 }
