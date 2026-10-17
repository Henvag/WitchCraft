param([string]$JavaBin=(Join-Path $env:LOCALAPPDATA 'SkyCraft\Prism\java\java-runtime-epsilon\bin'))
$ErrorActionPreference='Stop'
$taskOutput=Join-Path $PSScriptRoot 'classes-tests'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
& (Join-Path $JavaBin 'javac.exe') --release 25 -d $taskOutput (Join-Path $PSScriptRoot 'src\dev\skycraft\client\GroundRecovery.java') (Join-Path $PSScriptRoot 'GroundRecoveryTest.java')
if($LASTEXITCODE -ne 0){throw 'Ground recovery test compilation failed'}
& (Join-Path $JavaBin 'java.exe') -cp $taskOutput GroundRecoveryTest
if($LASTEXITCODE -ne 0){throw 'Ground recovery regressions failed'}
