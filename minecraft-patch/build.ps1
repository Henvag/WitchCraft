param([string]$BaseMod=(Join-Path $env:LOCALAPPDATA 'SkyCraft\Prism\instances\SkyCraft\.minecraft\mods\skycraft-0.1.2.jar'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem
$expected='EA429C73DCEC49D39B6A0BB9B17AD08A6B0290A39A96CCF6F6C07D581CF992D9'
if((Get-FileHash -LiteralPath $BaseMod).Hash -ne $expected){throw 'Expected original SkyCraft 0.1.2. Pass -BaseMod pointing to its backed-up original JAR.'}
$prismRoot=Join-Path $env:LOCALAPPDATA 'SkyCraft\Prism'
$jdk=Join-Path $prismRoot 'java\java-runtime-epsilon\bin'
$classes=Join-Path $PSScriptRoot 'classes'
New-Item -ItemType Directory -Force -Path $classes | Out-Null
$dependencies=@(Get-ChildItem -LiteralPath (Join-Path $prismRoot 'libraries') -Filter '*.jar' -Recurse -File | Where-Object {$_.FullName -notmatch 'natives-windows-(x86|arm64)'} | ForEach-Object {$_.FullName})
$classpath=($dependencies+$BaseMod)-join ';'
$arguments=@('--release','25','-classpath',('"'+$classpath.Replace('\','/')+'"'),'-d',('"'+$classes.Replace('\','/')+'"'))
foreach($name in @('FrameExporter','PersistentOverlayReadback','SkyClient','GroundRecovery','TerrainGuard')) {
    $source=Join-Path $PSScriptRoot "src\dev\skycraft\client\$name.java"
    $arguments+=('"'+$source.Replace('\','/')+'"')
}
$argPath=Join-Path $PSScriptRoot 'javac.args'
$arguments | Set-Content -LiteralPath $argPath
& (Join-Path $jdk 'javac.exe') "@$argPath"
if($LASTEXITCODE -ne 0){throw 'Java compilation failed'}
$outputDirectory=Join-Path (Split-Path $PSScriptRoot -Parent) 'dist\minecraft'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$output=Join-Path $outputDirectory 'skycraft-0.1.2.jar'
Copy-Item -LiteralPath $BaseMod -Destination $output -Force
& (Join-Path $jdk 'jar.exe') --update --file $output -C $classes dev/skycraft/client
if($LASTEXITCODE -ne 0){throw 'JAR update failed'}
# Preserve every unrelated upstream entry byte-for-byte.
$old=[IO.Compression.ZipFile]::OpenRead($BaseMod)
$new=[IO.Compression.ZipFile]::OpenRead($output)
try {
    $changed=@('dev/skycraft/client/FrameExporter.class','dev/skycraft/client/FrameExporter$Staging.class')
    foreach($entry in $old.Entries) {
        if($entry.FullName.EndsWith('/') -or $entry.FullName -in $changed -or
            $entry.FullName -eq 'dev/skycraft/client/SkyClient.class' -or
            $entry.FullName.StartsWith('dev/skycraft/client/SkyClient$')){continue}
        $other=$new.GetEntry($entry.FullName)
        if(!$other){throw "Missing original entry: $($entry.FullName)"}
        $a=$entry.Open()
        $b=$other.Open()
        try {
            $hash=[Security.Cryptography.SHA256]::Create()
            $ha=[BitConverter]::ToString($hash.ComputeHash($a)).Replace('-','')
            $hb=[BitConverter]::ToString($hash.ComputeHash($b)).Replace('-','')
            $hash.Dispose()
            if($ha -ne $hb){throw "Unrelated entry changed: $($entry.FullName)"}
        } finally {$a.Dispose();$b.Dispose()}
    }
    if(!$new.GetEntry('dev/skycraft/client/PersistentOverlayReadback.class')){throw 'Readback class missing'}
} finally {$old.Dispose();$new.Dispose()}
Get-FileHash -LiteralPath $output
