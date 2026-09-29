param([string]$Sdk=(Join-Path $env:LOCALAPPDATA 'Android/Sdk'),[string]$Ndk=(Join-Path $env:LOCALAPPDATA 'Android/Sdk/ndk/27.3.13750724'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$src=Join-Path $root 'experimental/dynarmic/apk'
$build=Join-Path $root 'out/dynarmic-apk'
$bt=Join-Path $Sdk 'build-tools/35.0.0'
$jar=Join-Path $Sdk 'platforms/android-36/android.jar'
function Run([string]$Exe,[string[]]$Arguments) { & $Exe @Arguments; if($LASTEXITCODE){throw "$Exe failed ($LASTEXITCODE)"} }
New-Item -ItemType Directory -Force "$build/classes","$build/stage/lib/arm64-v8a" | Out-Null
Run "$Ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe" @('--target=aarch64-linux-android29','-shared','-static-libstdc++','-fPIC','-O2',"$src/probe.cpp",'-llog','-o',"$build/stage/lib/arm64-v8a/libdynarmicprobe.so")
Run javac @('-source','8','-target','8','-bootclasspath',$jar,'-d',"$build/classes","$src/MainActivity.java")
Run "$bt/d8.bat" @('--lib',$jar,'--output',"$build/stage","$build/classes/com/axrb/dynarmicprobe/MainActivity.class")
Run "$bt/aapt2.exe" @('link','-I',$jar,'--manifest',"$src/AndroidManifest.xml",'-o',"$build/unsigned.apk")
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::Open("$build/unsigned.apk",'Update')
try {
    foreach($item in @('classes.dex','lib/arm64-v8a/libdynarmicprobe.so')) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,"$build/stage/$item",$item,'Optimal') | Out-Null
    }
} finally {$zip.Dispose()}
$key=Join-Path $build 'probe.keystore'
$keytool=Get-Command keytool -ErrorAction SilentlyContinue
if($keytool){$keytool=$keytool.Source} else {$keytool=Join-Path $env:ProgramFiles 'Android/Android Studio/jbr/bin/keytool.exe'}
if(-not(Test-Path $key)) { Run $keytool @('-genkeypair','-keystore',$key,'-storepass','android','-keypass','android','-alias','probe','-keyalg','RSA','-validity','3650','-dname','CN=AXRB Probe') }
Run "$bt/zipalign.exe" @('-f','-p','4',"$build/unsigned.apk","$build/aligned.apk")
Run "$bt/apksigner.bat" @('sign','--ks',$key,'--ks-pass','pass:android','--out',"$build/dynarmic-probe.apk","$build/aligned.apk")
Write-Output "$build/dynarmic-probe.apk"
