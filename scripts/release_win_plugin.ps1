# End-to-end Windows release for one plugin.
#   .\scripts\release_win_plugin.ps1 -Plugin duck -Upload
param(
    [Parameter(Mandatory = $true)]
    [string]$Plugin,
    [switch]$SkipVerify,
    [switch]$SkipBuild,
    [switch]$Upload,
    [string]$BuildDir = $(if ($env:AA_BUILD_DIR) { $env:AA_BUILD_DIR } else { "build" })
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$metaJson = python -c @"
import json, sys
sys.path.insert(0, 'scripts')
from lib.plugin_release import get_plugin
p = get_plugin('$Plugin')
print(json.dumps({
    'slug': p.slug,
    'version': p.version,
    'target': p.cmake_target,
    'zip': p.win_zip_name(),
    'dist': str(p.release_root()).replace('\\', '/'),
}))
"@
$meta = $metaJson | ConvertFrom-Json
$env:AA_PLUGIN_VERSION = $meta.version

function Invoke-Step([string]$Label, [scriptblock]$Block) {
    Write-Host ""
    Write-Host "== $Label"
    $global:LASTEXITCODE = 0
    & $Block
    if ($LASTEXITCODE -ne 0) {
        throw "Step failed ($Label): exit code $LASTEXITCODE"
    }
}

if (-not $SkipBuild) {
    Invoke-Step "configure (Release, plugin=$($meta.slug))" {
        cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=Release `
            "-DAA_PLUGINS=$($meta.slug)" -DAA_LOCAL_BUILD=OFF -DAA_BUILD_TESTS=ON
    }
    Invoke-Step "build $($meta.target)_VST3 + tests" {
        cmake --build $BuildDir --config Release --target "$($meta.target)_VST3" "$($meta.target)Tests" --parallel
    }
}

if (-not $SkipVerify) {
    $slugPattern = [regex]::Escape($meta.slug)
    Invoke-Step "ctest (release gate, no real-mouse)" {
        ctest --test-dir $BuildDir -C Release --output-on-failure `
            -R "^(dsp|core|$slugPattern)\." -E "real mouse"
    }
}

Invoke-Step "package zip" {
    & (Join-Path $PSScriptRoot "package_win_plugin.ps1") -Plugin $Plugin
}

# release_root() is already absolute. Join-Path does not drop an absolute child, so
# prefixing $RepoRoot produced D:\repo\D:\repo\dist\... and the zip check failed.
$distDir = $meta.dist -replace '/', '\'
$artifact = Join-Path $distDir $meta.zip
$manifest = Join-Path $distDir "manifest-win.json"
if (-not (Test-Path $artifact)) {
    throw "Release zip not found: $artifact"
}

Invoke-Step "validate" {
    python scripts/validate_plugin_release.py --plugin $Plugin --platform win --artifact $artifact `
        --version $meta.version --allow-unsigned --write-manifest $manifest
}

if ($Upload) {
    Invoke-Step "upload to Supabase" {
        $env:AA_WEB_REPO = if ($env:AA_WEB_REPO) { $env:AA_WEB_REPO } else { Join-Path (Split-Path $RepoRoot -Parent) "animal-audio-web" }
        python scripts/upload_plugin_release.py --manifest $manifest --zip $artifact
    }
}

Write-Host ""
Write-Host "Windows release finished ($($meta.slug) $($meta.version))."
Write-Host "  zip:      $artifact"
Write-Host "  manifest: $manifest"
