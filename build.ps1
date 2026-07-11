param(
    [switch]$Debug,
    [switch]$Clean,
    [switch]$NoSign
)

$ErrorActionPreference = "Stop"
$srcDir = "$PSScriptRoot\src"
$output = "$PSScriptRoot\tfm.exe"
$certPfx = "$PSScriptRoot\tfm-cert.pfx"
$certSubject = "CN=tfm Code Signing"

$msys2 = "C:\msys64\ucrt64"
$gcc   = "$msys2\bin\gcc.exe"

# ensure MSYS2 bin is in PATH so libssh2 DLLs are found at link time
$env:PATH = "$msys2\bin;$env:PATH"

if ($Clean) {
    Write-Host "Cleaning..."
    Remove-Item -Path "$srcDir\*.o" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $output -Force -ErrorAction SilentlyContinue
    Write-Host "Done."
    exit 0
}

$cFlags = @("-static", "-std=c11", "-Wall", "-Wextra")
$ldFlags = @("-static", "-lssh2", "-lz", "-lssl", "-lcrypto",
             "-lcrypt32", "-lws2_32", "-luser32", "-lkernel32", "-lshell32", "-lshlwapi", "-lole32")

if ($Debug) {
    $cFlags += "-g", "-O0"
    Write-Host "Building DEBUG configuration..."
} else {
    $cFlags += "-O2", "-s"
    Write-Host "Building RELEASE configuration (static, stripped)..."
}

$srcFiles = Get-ChildItem -Path "$srcDir\*.c" | Sort-Object Name
if ($srcFiles.Count -eq 0) {
    Write-Error "No source files found in $srcDir"
    exit 1
}

Write-Host "Compiling $($srcFiles.Count) source files..."
$fullArgs = (@($cFlags) + $srcFiles.FullName + @("-o", $output) + $ldFlags) -join " "
Write-Host "gcc $fullArgs"

& $gcc @cFlags $srcFiles.FullName -o $output @ldFlags 2>&1 | ForEach-Object {
    if ($_ -match "error:") { Write-Host $_ -ForegroundColor Red }
    elseif ($_ -match "warning:") { Write-Host $_ -ForegroundColor Yellow }
    else { Write-Host $_ }
}
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

$size = (Get-Item $output).Length
Write-Host ("Build successful: {0} ({1:F0} KB)" -f $output, ($size / 1KB))

# --------------- line counts ---------------
Write-Host "`n  File            Lines"
Write-Host "  ----            -----"
$total = 0
foreach ($f in $srcFiles) {
    $lines = (Get-Content $f.FullName | Measure-Object -Line).Lines
    $total += $lines
    Write-Host ("  {0,-15} {1,5}" -f $f.Name, $lines)
}
Write-Host ("  {0,-15} {1,5}" -f "----", "-----")
Write-Host ("  {0,-15} {1,5} total" -f "$($srcFiles.Count) files", $total)

# --------------- code signing ---------------
if ($NoSign) {
    Write-Host "`nSkipping code signing (--NoSign)."
    exit 0
}

Write-Host "`nSigning $output ..."

# look for existing cert in the user's personal store
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Where-Object { $_.Subject -eq $certSubject } |
        Select-Object -First 1

if (!$cert) {
    Write-Host "  No existing certificate found. Creating new self-signed certificate..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert `
        -Subject $certSubject `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyUsage DigitalSignature

    # also export a PFX to the project dir for reference
    $pfxPass = ConvertTo-SecureString -String "tfm" -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $certPfx -Password $pfxPass | Out-Null
    Write-Host "  Certificate exported to $certPfx"
}

try {
    Set-AuthenticodeSignature -FilePath $output -Certificate $cert `
        -TimestampServer "http://timestamp.digicert.com" -ErrorAction Stop | Out-Null
    Write-Host "  Signed successfully (thumbprint: $($cert.Thumbprint))"
} catch {
    Write-Warning "  Signature timestamp server unavailable; signing without timestamp."
    Set-AuthenticodeSignature -FilePath $output -Certificate $cert -ErrorAction SilentlyContinue | Out-Null
}
