param(
    [switch]$Debug,
    [switch]$Clean
)

$srcDir = "$PSScriptRoot\src"
$output = "$PSScriptRoot\tfm.exe"

if ($Clean) {
    Write-Host "Cleaning..."
    Remove-Item -Path "$srcDir\*.o" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $output -Force -ErrorAction SilentlyContinue
    Write-Host "Done."
    exit 0
}

$cFlags = @("-static", "-std=c11", "-Wall", "-Wextra")
$ldFlags = @("-static", "-luser32", "-lkernel32", "-lshell32", "-lshlwapi", "-lole32")

if ($Debug) {
    $cFlags += "-g"
    $cFlags += "-O0"
    Write-Host "Building DEBUG configuration..."
} else {
    $cFlags += "-O2"
    $cFlags += "-s"
    Write-Host "Building RELEASE configuration (static, stripped)..."
}

$srcFiles = Get-ChildItem -Path "$srcDir\*.c" | ForEach-Object { $_.FullName }

if ($srcFiles.Count -eq 0) {
    Write-Error "No source files found in $srcDir"
    exit 1
}

Write-Host "Compiling $($srcFiles.Count) source files..."

$gccArgs = @($cFlags) + $srcFiles + @("-o", $output) + $ldFlags
$gccArgsFlat = $gccArgs -join " "

Write-Host "gcc $gccArgsFlat"

$process = Start-Process -FilePath "gcc" -ArgumentList $gccArgsFlat -NoNewWindow -Wait -PassThru

if ($process.ExitCode -eq 0) {
    $size = (Get-Item $output).Length
    Write-Host "Build successful: $output ($($size / 1KB) KB)"
} else {
    Write-Error "Build failed with exit code $($process.ExitCode)"
    exit $process.ExitCode
}
