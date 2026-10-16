$ErrorActionPreference = 'Stop'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'scripts/build/direct_deps.ps1'),[ref]$tokens,[ref]$errors)
if($errors) { throw ($errors|Out-String) }
$function=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-AxrbPatchApplied'},$true)
Invoke-Expression $function.Extent.Text
$fixture=(Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) ('out/patch-check-' + [guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Force $fixture | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'git.cmd'),"@echo expected reverse-check failure 1>&2`r`n@exit /b 1`r`n")
$env:PATH=$fixture+';'+$env:PATH
if(Test-AxrbPatchApplied 'repo with spaces' 'patch with spaces') { throw 'Expected a false probe result' }
if($ErrorActionPreference -ne 'Stop') { throw 'Error preference was not restored' }
[IO.File]::WriteAllText((Join-Path $fixture 'git.cmd'),"@exit /b 0`r`n")
if(!(Test-AxrbPatchApplied 'repo with spaces' 'patch with spaces')) { throw 'Expected a true probe result' }
Write-Output 'Windows PowerShell expected-stderr probe handling passed.'
