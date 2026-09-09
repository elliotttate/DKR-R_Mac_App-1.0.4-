[CmdletBinding()]
param(
    [string]$Version = '',
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build\dkr-runtime-rt64',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $distRoot = Join-Path $projectRoot 'dist'
} else {
    $distRoot = [IO.Path]::GetFullPath($OutputDirectory)
}
if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    $resolvedBuild = [IO.Path]::GetFullPath($BuildDirectory)
} else {
    $resolvedBuild = Join-Path $projectRoot $BuildDirectory
}
$stage = Join-Path $distRoot "DKR-R-$Version-Windows-x64"
$zip = "$stage.zip"
$deniedExtensions = @('.z64', '.v64', '.n64', '.eep', '.mpk', '.sra', '.fla', '.o2r', '.otr')
$runtimeFiles = @('DKR-R.exe', 'SDL2.dll', 'dxcompiler.dll', 'dxil.dll')
$inputHostFiles = @('DKR-R-InputHost.exe', 'SDL3.dll')
$modelFamilies = @('palm', 'blueberry', 'rubber-tree', 'beach-tree', 'banana', 'balloons')
$modelHashes = [ordered]@{}

if (Test-Path -LiteralPath $stage) {
    throw "Refusing to overwrite existing release directory: $stage"
}
if (Test-Path -LiteralPath $zip) {
    throw "Refusing to overwrite existing release archive: $zip"
}

$bin = Join-Path $resolvedBuild "bin\$Configuration"
foreach ($name in $runtimeFiles) {
    $source = Join-Path $bin $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing release runtime file: $source"
    }
}
$inputHostBin = Join-Path $bin 'libexec\dkr-r'
foreach ($name in $inputHostFiles) {
    $source = Join-Path $inputHostBin $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing private SDL3 input host file: $source"
    }
}

# Package the assets staged with this binary. Every model file must match the
# checkout, so stale or incomplete build output cannot silently ship sprites
# in place of the fork's 3D replacements.
foreach ($family in $modelFamilies) {
    $sourceModels = Join-Path $projectRoot "assets\models\$family"
    $builtModels = Join-Path $bin "assets\models\$family"
    $requiredModels = @('near.dkrmesh', 'far.dkrmesh', 'manifest.json', 'rt64.json')
    if ($family -eq 'palm') {
        $requiredModels += @('canopy-near.dkrmesh', 'canopy-far.dkrmesh')
    } elseif ($family -eq 'balloons') {
        $requiredModels += @('collectible-near.dkrmesh', 'collectible-far.dkrmesh')
    }
    foreach ($name in $requiredModels) {
        if (-not (Test-Path -LiteralPath (Join-Path $sourceModels $name) -PathType Leaf)) {
            throw "Missing required 3D model asset: $family/$name"
        }
    }
    $sourceFiles = @(Get-ChildItem -LiteralPath $sourceModels -Recurse -File)
    if (@($sourceFiles | Where-Object Extension -eq '.png').Count -eq 0) {
        throw "Missing 3D model textures: $sourceModels"
    }
    foreach ($file in $sourceFiles) {
        $relative = $file.FullName.Substring($sourceModels.Length + 1)
        $builtFile = Join-Path $builtModels $relative
        if (-not (Test-Path -LiteralPath $builtFile -PathType Leaf)) {
            throw "Missing staged 3D model asset: $builtFile. Rebuild before packaging."
        }
        $expectedHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if ((Get-FileHash -LiteralPath $builtFile -Algorithm SHA256).Hash -ne $expectedHash) {
            throw "Stale staged 3D model asset: $builtFile. Rebuild before packaging."
        }
        $modelHashes["assets/models/$family/$($relative.Replace('\', '/'))"] = $expectedHash
    }
}

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($name in $runtimeFiles) {
    Copy-Item -LiteralPath (Join-Path $bin $name) -Destination (Join-Path $stage $name)
}
$stagedInputHostDirectory = Join-Path $stage 'libexec\dkr-r'
New-Item -ItemType Directory -Path $stagedInputHostDirectory -Force | Out-Null
foreach ($name in $inputHostFiles) {
    Copy-Item -LiteralPath (Join-Path $inputHostBin $name) `
        -Destination (Join-Path $stagedInputHostDirectory $name)
}
$stagedRuntime = Join-Path $stage 'DKR-R.exe'
$stagedInputHost = Join-Path $stagedInputHostDirectory 'DKR-R-InputHost.exe'
$logoDirectory = Join-Path $stage 'assets\ui\Icons'
New-Item -ItemType Directory -Path $logoDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\ui\Icons\DKR-R-Logo.bmp') `
    -Destination (Join-Path $logoDirectory 'DKR-R-Logo.bmp')
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\ui\Icons\DKR-R-Spinning-Icon.png') `
    -Destination (Join-Path $logoDirectory 'DKR-R-Spinning-Icon.png')
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\ui\Icons\DKR-R-Short-Logo.png') `
    -Destination (Join-Path $logoDirectory 'DKR-R-Short-Logo.png')
$backgroundDirectory = Join-Path $stage 'assets\ui\Backgrounds'
New-Item -ItemType Directory -Path $backgroundDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\ui\Backgrounds\DKR-R-Launcher-Background.png') `
    -Destination (Join-Path $backgroundDirectory 'DKR-R-Launcher-Background.png')
$filterDirectory = Join-Path $stage 'assets\filters'
New-Item -ItemType Directory -Path $filterDirectory -Force | Out-Null
Copy-Item -Path (Join-Path $projectRoot 'assets\filters\*.png') `
    -Destination $filterDirectory -Force
$controllerDirectory = Join-Path $stage 'assets\controllers'
New-Item -ItemType Directory -Path $controllerDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\controllers\gamecontrollerdb.txt') `
    -Destination (Join-Path $controllerDirectory 'gamecontrollerdb.txt')
$modelDirectory = Join-Path $stage 'assets\models'
New-Item -ItemType Directory -Path $modelDirectory -Force | Out-Null
foreach ($family in $modelFamilies) {
    Copy-Item -LiteralPath (Join-Path $bin "assets\models\$family") `
        -Destination $modelDirectory -Recurse
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging\WINDOWS-README.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging\WINDOWS-3D-MODELS.md') `
    -Destination (Join-Path $stage '3D-MODELS.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\ONLINE_MULTIPLAYER.md') -Destination (Join-Path $stage 'ONLINE_MULTIPLAYER.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE.md') -Destination (Join-Path $stage 'LICENSE.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY.md') -Destination (Join-Path $stage 'THIRD_PARTY.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'runtime-recomp\COPYING-NOTICE.md') -Destination (Join-Path $stage 'COPYING-NOTICE.md')
$noticeDirectory = Join-Path $stage 'ThirdPartyLicenses'
New-Item -ItemType Directory -Path $noticeDirectory | Out-Null
$noticeFiles = [ordered]@{
    'RT64-LICENSE.txt' = 'extern\rt64\LICENSE'
    'Dear-ImGui-LICENSE.txt' = 'extern\rt64\src\contrib\imgui\LICENSE.txt'
    'SDL2-LICENSE.txt' = 'extern\rt64\src\contrib\mupen64plus-win32-deps\SDL2-2.26.3\COPYING.txt'
    'SDL3-LICENSE.txt' = 'extern\sdl3\LICENSE.txt'
    'N64ModernRuntime-COPYING.txt' = 'extern\n64-modern-runtime\COPYING'
    'N64Recomp-LICENSE.txt' = 'extern\n64-modern-runtime\N64Recomp\LICENSE'
    'DXC-NOTICE.md' = 'packaging\licenses\DXC-NOTICE.md'
    'Jumpman-LICENSE.txt' = 'packaging\licenses\Jumpman-LICENSE.txt'
    'CRT-FILTERS-NOTICE.md' = 'packaging\licenses\CRT-FILTERS-NOTICE.md'
    'SDL-GAMECONTROLLERDB-LICENSE.txt' = 'packaging\licenses\SDL-GAMECONTROLLERDB-LICENSE.txt'
    'GEKKONET-LICENSE.txt' = 'packaging\licenses\GEKKONET-LICENSE.txt'
    'MONOCYPHER-LICENSE.txt' = 'packaging\licenses\MONOCYPHER-LICENSE.txt'
    'LIBDATACHANNEL-LICENSE.txt' = 'extern\libdatachannel\LICENSE'
    'MBEDTLS-LICENSE.txt' = 'extern\mbedtls\LICENSE'
    'LIBJUICE-LICENSE.txt' = 'extern\libdatachannel\deps\libjuice\LICENSE'
    'USRSCTP-LICENSE.txt' = 'extern\libdatachannel\deps\usrsctp\LICENSE.md'
    'NLOHMANN-JSON-LICENSE.txt' = 'extern\libdatachannel\deps\json\LICENSE.MIT'
    'PLOG-LICENSE.txt' = 'extern\libdatachannel\deps\plog\LICENSE'
}

# SDL3 is deliberately private to the controller helper. Exercise it from the
# exact release path before the public launcher is tested; this catches a
# missing SDL3.dll without ever loading SDL3 into the launcher/game process.
$inputHostTest = [Diagnostics.Process]::Start(
    $stagedInputHost,
    "--self-test --mappings `"$(Join-Path $controllerDirectory 'gamecontrollerdb.txt')`"")
$inputHostTest.WaitForExit()
if ($inputHostTest.ExitCode -ne 0) {
    $exitCode = $inputHostTest.ExitCode
    $inputHostTest.Dispose()
    throw "The staged private SDL3 input host failed its self-test with exit code $exitCode."
}
$inputHostTest.Dispose()
foreach ($entry in $noticeFiles.GetEnumerator()) {
    $source = Join-Path $projectRoot $entry.Value
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing third-party notice: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $noticeDirectory $entry.Key)
}

# Exercise the binary from the exact staged package, not only from the build
# tree. This catches a missing DLL or packaging-path regression before ZIP
# creation while keeping all test data outside the release directory.
$pakTest = Join-Path ([IO.Path]::GetTempPath()) `
    ("dkr-r-packaged-pak-" + [Guid]::NewGuid().ToString('N'))
$selfTestProcess = $null
try {
    $selfTestInfo = [Diagnostics.ProcessStartInfo]::new()
    $selfTestInfo.FileName = $stagedRuntime
    $selfTestInfo.Arguments = "--self-test-pak `"$pakTest`""
    $selfTestInfo.UseShellExecute = $false
    $selfTestInfo.CreateNoWindow = $true
    $selfTestProcess = [Diagnostics.Process]::Start($selfTestInfo)
    $selfTestProcess.WaitForExit()
    if ($selfTestProcess.ExitCode -ne 0) {
        throw "The staged Windows runtime failed its Controller Pak self-test with exit code $($selfTestProcess.ExitCode)."
    }
} finally {
    if ($null -ne $selfTestProcess) {
        $selfTestProcess.Dispose()
    }
    if (Test-Path -LiteralPath $pakTest) {
        Remove-Item -LiteralPath $pakTest -Recurse -Force
    }
}

# Verify the exact staged launcher can hand controller ownership to the private
# SDL3 helper and back twice without shutting down SDL2 video or audio.
$inputSwitchTest = Join-Path ([IO.Path]::GetTempPath()) `
    ("dkr-r-packaged-input-switch-" + [Guid]::NewGuid().ToString('N'))
$inputSwitchProcess = $null
try {
    New-Item -ItemType Directory -Path $inputSwitchTest | Out-Null
    $inputSwitchInfo = [Diagnostics.ProcessStartInfo]::new()
    $inputSwitchInfo.FileName = $stagedRuntime
    $inputSwitchInfo.Arguments =
        "--self-test-input-switch `"$inputSwitchTest`""
    $inputSwitchInfo.UseShellExecute = $false
    $inputSwitchInfo.CreateNoWindow = $true
    $inputSwitchInfo.EnvironmentVariables['SDL_VIDEODRIVER'] = 'dummy'
    $inputSwitchInfo.EnvironmentVariables['SDL_AUDIODRIVER'] = 'dummy'
    $inputSwitchProcess = [Diagnostics.Process]::Start($inputSwitchInfo)
    $inputSwitchProcess.WaitForExit()
    if ($inputSwitchProcess.ExitCode -ne 0) {
        throw "The staged Windows runtime failed its live input-switch self-test with exit code $($inputSwitchProcess.ExitCode)."
    }
} finally {
    if ($null -ne $inputSwitchProcess) {
        $inputSwitchProcess.Dispose()
    }
    if (Test-Path -LiteralPath $inputSwitchTest) {
        Remove-Item -LiteralPath $inputSwitchTest -Recurse -Force
    }
}

$packagedFiles = Get-ChildItem -LiteralPath $stage -Recurse -File
$executables = @($packagedFiles | Where-Object { $_.Extension -ieq '.exe' })
$expectedExecutables = @(
    [IO.Path]::GetFullPath($stagedRuntime),
    [IO.Path]::GetFullPath($stagedInputHost)
)
$unexpectedExecutables = @($executables | Where-Object {
    $expectedExecutables -notcontains [IO.Path]::GetFullPath($_.FullName)
})
if ($executables.Count -ne 2 -or $unexpectedExecutables.Count -ne 0) {
    throw "The Windows release must contain one public DKR-R.exe and one private libexec input host. Found: $($executables.FullName -join ', ')"
}
$denied = @($packagedFiles | Where-Object { $deniedExtensions -contains $_.Extension.ToLowerInvariant() })
if ($denied.Count -ne 0) {
    throw "Release staging contains prohibited game data: $($denied.FullName -join ', ')"
}

$n64Headers = @('80371240', '37804012', '40123780')
foreach ($file in $packagedFiles) {
    $stream = [System.IO.File]::OpenRead($file.FullName)
    try {
        $header = New-Object byte[] 4
        $read = $stream.Read($header, 0, 4)
        if ($read -eq 4) {
            $magic = [System.BitConverter]::ToString($header).Replace('-', '')
            if ($n64Headers -contains $magic) {
                throw "Release staging contains an N64 ROM header: $($file.FullName)"
            }
        }
    } finally {
        $stream.Dispose()
    }
}

$archiveCreated = $false
for ($attempt = 1; $attempt -le 5 -and -not $archiveCreated; $attempt++) {
    try {
        # Windows Defender and other scanners may briefly retain the staged PE
        # after its embedded-resource self-test. Retry that transient sharing
        # violation without weakening any package validation.
        Start-Sleep -Milliseconds (250 * $attempt)
        Compress-Archive -LiteralPath $stage -DestinationPath $zip `
            -CompressionLevel Optimal
        $archiveCreated = $true
    } catch {
        if (Test-Path -LiteralPath $zip) {
            Remove-Item -LiteralPath $zip -Force
        }
        if ($attempt -eq 5) {
            throw
        }
    }
}
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    $badEntries = @($archive.Entries | Where-Object {
        $deniedExtensions -contains [System.IO.Path]::GetExtension($_.FullName).ToLowerInvariant()
    })
    if ($badEntries.Count -ne 0) {
        throw "Release ZIP contains prohibited game data: $($badEntries.FullName -join ', ')"
    }
    # Windows PowerShell 5 writes backslashes in ZIP entry names; PowerShell
    # 7 uses forward slashes. Compare the same relative paths on both hosts.
    $archiveEntries = @{}
    foreach ($entry in $archive.Entries) {
        $archiveEntries[$entry.FullName.Replace('\', '/')] = $entry
    }
    foreach ($model in $modelHashes.GetEnumerator()) {
        $entryName = "$(Split-Path -Leaf $stage)/$($model.Key)"
        $entry = $archiveEntries[$entryName]
        if ($null -eq $entry) {
            throw "Release ZIP is missing 3D model asset: $entryName"
        }
        $stream = $entry.Open()
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            $actualHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '')
            if ($actualHash -ne $model.Value) {
                throw "Release ZIP contains an incorrect 3D model asset: $entryName"
            }
        } finally {
            $sha256.Dispose()
            $stream.Dispose()
        }
    }
} finally {
    $archive.Dispose()
}

$hash = Get-FileHash -LiteralPath $zip -Algorithm SHA256
Write-Host "Created $zip"
Write-Host "Verified $($modelHashes.Count) 3D model files across all $($modelFamilies.Count) families in the ZIP."
Write-Host "SHA-256 $($hash.Hash)"
