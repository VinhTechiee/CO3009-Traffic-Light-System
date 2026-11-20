param([string]$Compiler = 'gcc')

$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $workspace 'build/host-tests'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$flags = @('-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
           '-Wconversion', '-Wshadow', '-I', (Join-Path $workspace 'Core/Inc'))
$suites = [ordered]@{
    dht20_protocol = @('dht20_protocol')
    dht20_driver = @('dht20', 'dht20_protocol')
    lcd_format = @('lcd_format')
    lcd_presentation = @('lcd_presentation', 'lcd_format')
    fsm_tuning = @('fsm_tuning')
    fsm_mode = @('fsm_mode', 'fsm_tuning', 'fsm_automatic', 'fsm_manual',
                 'software_timer', 'button', 'traffic_light')
    lcd = @('lcd', 'lcd_display', 'lcd_presentation', 'lcd_format', 'dht20', 'dht20_protocol')
}

foreach ($suite in $suites.Keys) {
    $sources = @($suites[$suite] | ForEach-Object { Join-Path $workspace "Core/Src/$_.c" })
    $fixture = Join-Path $workspace "tests/test_$suite.c"
    $binary = Join-Path $buildDir "test_$suite.exe"
    & $Compiler @flags @sources $fixture '-o' $binary
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed for $suite (exit $LASTEXITCODE)" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "Tests failed for $suite (exit $LASTEXITCODE)" }
}
