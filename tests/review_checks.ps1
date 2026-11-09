# Offline source-contract checks. No network, device, or credential access.
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
function Assert-Review($condition, $message) {
    if (-not $condition) { throw $message }
}
$cpp = Get-Content valloxesp/Vallox.cpp -Raw
$header = Get-Content valloxesp/Vallox.h -Raw
$ino = Get-Content valloxesp/valloxesp.ino -Raw
$baseline = (git show origin/master:valloxesp/valloxesp.ino) -join "`n"
$oldKeys = [regex]::Matches($baseline, 'root\["([^"]+)"\]') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
foreach ($key in $oldKeys) {
    Assert-Review ($ino.Contains('root["' + $key + '"]')) "Removed legacy key: $key"
}
foreach ($file in @('sensor.py','binary_sensor.py','climate.py','switch.py')) {
    $old = (git show "origin/master:custom_components/vallox2mqtt/$file") -join "`n"
    $new = Get-Content "custom_components/vallox2mqtt/$file" -Raw
    $ids = [regex]::Matches($old, 'return f"\{DOMAIN\}_[^"]+"')
    foreach ($id in $ids) { Assert-Review ($new.Contains($id.Value)) "Changed unique ID in $file" }
}
$poll = [regex]::Match($cpp, '(?s)void Vallox::processRequestQueue\(\).*?(?=// tries to read)').Value
Assert-Review ($poll.Contains('message[2] = VX_MSG_MAINBOARD_1;')) 'Poll receiver changed'
Assert-Review ($poll.Contains('message[3] = VX_MSG_POLL_BYTE;')) 'Poll became a register write'
Assert-Review ($poll.Contains('message[4] = variable;')) 'Poll register missing'
Assert-Review (-not $poll.Contains('delay(')) 'Blocking poll delay'
Assert-Review ($poll.Contains('now - lastBusActivity < REQUEST_GAP')) 'Missing bus-idle guard'
Assert-Review ($cpp.Contains('REQUEST_QUEUE_LENGTH')) 'Missing request queue bound'
Assert-Review ($cpp.Contains('WRITE_QUEUE_LENGTH') -or $header.Contains('WRITE_QUEUE_LENGTH')) 'Missing write queue bound'
Assert-Review ($cpp.Contains('WRITE_HOLD_MS') -or $header.Contains('WRITE_HOLD_MS')) 'Missing write hold timeout'
Assert-Review (-not $cpp.Contains('WRITE_DEFER_MAX_MS')) 'Forced busy-bus send still present'
Assert-Review ($ino.Contains('deserializeJson(d, (const char*)payload, length)')) 'Unbounded MQTT parsing'
Assert-Review ($ino.Contains('beginPublish(vallox_state_topic')) 'State streaming removed'
Assert-Review ([regex]::Match($cpp, '(?s)void Vallox::requestConfig.*?void Vallox::loop').Value.Contains('sendRhReq();')) 'Startup RH request missing (kept for master-compatible boot)'
$tempBody = [regex]::Match($cpp, '(?s)const int8_t vxTemps\[\] = \{(.*?)\};').Groups[1].Value
$tempBody = [regex]::Replace($tempBody, '//[^\r\n]*', '')
Assert-Review ([regex]::Matches($tempBody, '-?\d+').Count -eq 256) 'NTC table does not cover all byte indices'
foreach ($file in Get-ChildItem custom_components/vallox2mqtt -Recurse -Filter *.json) {
    Get-Content $file.FullName -Raw | ConvertFrom-Json | Out-Null
}
$protected = git diff HEAD -- valloxesp/valloxesp.h
Assert-Review (-not $protected) 'Protected credentials template changed'
git diff --check
Assert-Review ($LASTEXITCODE -eq 0) 'Whitespace errors'
Write-Output 'PASS: source contracts, legacy keys/IDs, poll framing, queue guard, JSON, NTC bounds, protected file.'
