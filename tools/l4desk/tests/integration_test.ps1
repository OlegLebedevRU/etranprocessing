# PowerShell wrapper for l4desk integration test
$ErrorActionPreference = "Stop"
Write-Host "Running l4desk end-to-end integration test via Python..." -ForegroundColor Cyan
python "$PSScriptRoot\integration_test.py"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Integration test failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}
Write-Host "Integration test passed successfully!" -ForegroundColor Green
exit 0
