param([string]$Compiler = 'gcc', [switch]$WslSanitizers)

$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$flags = @('-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
           '-Wconversion', '-Wshadow', '-I', 'Core/Inc')
$buildDir = 'build/host-tests'
if ($WslSanitizers) {
    $buildDir = 'build/host-sanitizers'
    $flags += @('-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', '-no-pie')
}
$suites = [ordered]@{
    dht20_protocol = @('dht20_protocol')
    dht20_driver = @('dht20', 'dht20_protocol')
    lcd_format = @('lcd_format')
    lcd_presentation = @('lcd_presentation', 'lcd_format')
    fsm_tuning = @('fsm_tuning')
    fsm_mode = @('fsm_mode', 'fsm_tuning', 'fsm_automatic', 'fsm_manual',
                 'software_timer', 'button', 'traffic_light')
    lcd = @('lcd', 'lcd_display', 'lcd_presentation', 'lcd_format', 'dht20', 'dht20_protocol')
    scheduler = @('scheduler', 'software_timer')
    fsm_automatic = @('fsm_automatic', 'software_timer')
    button_manual = @('button', 'traffic_light', 'fsm_manual')
    button_manual_alt = @('button', 'traffic_light', 'fsm_manual')
    i2c_stm32 = @('i2c_stm32')
    lcd_pcf8574 = @('lcd', 'lcd_pcf8574')
    app_runtime = @('app_runtime', 'scheduler', 'software_timer', 'button', 'traffic_light',
                    'fsm_mode', 'fsm_tuning', 'fsm_automatic', 'fsm_manual', 'dht20',
                    'dht20_protocol', 'lcd', 'lcd_display', 'lcd_presentation', 'lcd_format')
}
$suiteFlags = @{
    scheduler = @('-DSCH_HOST_TEST')
    app_runtime = @('-DSCH_HOST_TEST')
    i2c_stm32 = @('-I', 'tests/i2c_fake')
    button_manual_alt = @('-include', 'tests/fixtures/board-profile-validation.h')
}

Push-Location -LiteralPath $workspace
try {
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    foreach ($suite in $suites.Keys) {
        $sources = @($suites[$suite] | ForEach-Object { "Core/Src/$_.c" })
        $fixtureName = if ($suite -eq 'button_manual_alt') { 'button_manual' } else { $suite }
        $fixture = "tests/test_$fixtureName.c"
        $binary = "$buildDir/test_$suite"
        $compileArgs = $flags + @($suiteFlags[$suite] | Where-Object { $_ }) + $sources + @($fixture, '-o')
        if ($WslSanitizers) {
            & wsl.exe --cd $workspace -- $Compiler @compileArgs $binary
        } else {
            $binary += '.exe'
            & $Compiler @compileArgs $binary
        }
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed for $suite (exit $LASTEXITCODE)" }
        if ($WslSanitizers) {
            & wsl.exe --cd $workspace -- "./$binary"
        } else {
            & "./$binary"
        }
        if ($LASTEXITCODE -ne 0) { throw "Tests failed for $suite (exit $LASTEXITCODE)" }
    }
} finally {
    Pop-Location
}
