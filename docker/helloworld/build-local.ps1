[CmdletBinding()]
param(
    [string]$Image = "registry.chengyistudio.com/cxx/helloworld:0.2.0"
)

$ErrorActionPreference = "Stop"
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$composeFile = Join-Path $scriptDirectory "compose.yaml"
$previousImage = $env:HELLOWORLD_IMAGE

Push-Location $scriptDirectory

try {
    $env:HELLOWORLD_IMAGE = $Image

    Write-Host "Building and loading ARM64 image: $Image"
    & docker compose -f $composeFile build helloworld

    if ($LASTEXITCODE -ne 0) {
        throw "ARM64 image build failed."
    }

    $architecture = (& docker image inspect $Image --format "{{.Architecture}}").Trim()

    if ($LASTEXITCODE -ne 0) {
        throw "Built image was not loaded into the local Docker image store."
    }

    if ($architecture -ne "arm64") {
        throw "Unexpected image architecture: $architecture"
    }

    Write-Host "ARM64 image is ready in Docker Desktop: $Image"
}
finally {
    Pop-Location

    if ($null -eq $previousImage) {
        Remove-Item Env:HELLOWORLD_IMAGE -ErrorAction SilentlyContinue
    }
    else {
        $env:HELLOWORLD_IMAGE = $previousImage
    }
}
