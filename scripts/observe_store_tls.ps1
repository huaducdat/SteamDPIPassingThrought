param(
    [ValidateRange(1, 3)][int]$Attempts = 2,
    [ValidateRange(1, 2)][int]$MaxAddressesPerFamily = 2,
    [string]$OutputPath = "out/steam_store_observation.json"
)

$ErrorActionPreference = 'Continue'
$hostName = 'store.steampowered.com'
$curl = (Get-Command curl.exe -ErrorAction Stop).Source
$dnsResults = @()
$probes = @()

foreach ($family in @('A', 'AAAA')) {
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $addresses = @(Resolve-DnsName -Name $hostName -Type $family -DnsOnly -ErrorAction Stop |
            Where-Object { $_.IPAddress } |
            Select-Object -ExpandProperty IPAddress -Unique |
            Select-Object -First $MaxAddressesPerFamily)
        $clock.Stop()
        $dnsResults += [pscustomobject]@{
            RecordType = $family; Status = if ($addresses.Count) { 'SUCCESS' } else { 'NO_RECORDS' }
            LatencyMs = [math]::Round($clock.Elapsed.TotalMilliseconds, 1)
            Addresses = @($addresses); Error = $null
        }
    }
    catch {
        $clock.Stop()
        $dnsResults += [pscustomobject]@{
            RecordType = $family; Status = 'FAILURE'; LatencyMs = [math]::Round($clock.Elapsed.TotalMilliseconds, 1)
            Addresses = @(); Error = $_.Exception.Message
        }
    }
}

# The network probe is explicitly direct and pinned to the resolved address. It uses
# curl's normal Schannel certificate verification and does not change system settings.
foreach ($dns in $dnsResults) {
    foreach ($address in $dns.Addresses) {
        foreach ($timeoutSeconds in @(4, 8)) {
            for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
                $resolveAddress = if ($dns.RecordType -eq 'AAAA') { "[$address]" } else { $address }
                $resolve = "${hostName}:443:${resolveAddress}"
                $format = "`nSTEAMDPI_META|%{http_code}|%{time_namelookup}|%{time_connect}|%{time_appconnect}|%{time_starttransfer}|%{remote_ip}|%{errormsg}"
                $started = [DateTimeOffset]::Now
                $output = @(& $curl --silent --show-error --head --noproxy '*' --connect-timeout $timeoutSeconds `
                    --max-time ($timeoutSeconds + 3) --resolve $resolve --write-out $format "https://${hostName}/" 2>&1)
                $exitCode = $LASTEXITCODE
                $finished = [DateTimeOffset]::Now
                $joined = $output -join "`n"
                $metaLine = @($joined -split "`r?`n" | Where-Object { $_ -like 'STEAMDPI_META|*' } | Select-Object -Last 1)
                $httpCode = $null; $dnsSec = $null; $tcpSec = $null; $tlsSec = $null; $firstByteSec = $null; $remoteIp = $null; $curlError = $null
                if ($metaLine.Count) {
                    $fields = $metaLine[0] -split '\|', 8
                    if ($fields.Count -ge 8) {
                        $httpCode = $fields[1]; $dnsSec = $fields[2]; $tcpSec = $fields[3]; $tlsSec = $fields[4]
                        $firstByteSec = $fields[5]; $remoteIp = $fields[6]; $curlError = $fields[7]
                    }
                }
                $category = switch ($exitCode) {
                    0 { 'TLS_SUCCESS' }
                    6 { 'DNS_ERROR' }
                    7 { 'TCP_CONNECT_ERROR' }
                    28 {
                        if (!$tcpSec -or $tcpSec -eq '0.000000') { 'TCP_CONNECT_TIMEOUT' }
                        elseif (!$tlsSec -or $tlsSec -eq '0.000000') { 'TLS_HANDSHAKE_TIMEOUT' }
                        else { 'HTTP_RESPONSE_TIMEOUT' }
                    }
                    35 { 'TLS_HANDSHAKE_ERROR' }
                    60 { 'CERTIFICATE_ERROR' }
                    default { 'OTHER_ERROR' }
                }
                $probes += [pscustomobject]@{
                    AddressFamily = $dns.RecordType; Address = $address; Attempt = $attempt
                    ConnectTimeoutSeconds = $timeoutSeconds; StartedAt = $started.ToString('o'); FinishedAt = $finished.ToString('o')
                    CurlExitCode = $exitCode; Category = $category; HttpStatus = $httpCode
                    DnsSeconds = $dnsSec; TcpSeconds = $tcpSec; TlsHandshakeSeconds = $tlsSec
                    FirstByteSeconds = $firstByteSec; RemoteAddress = $remoteIp; Error = $curlError
                }
                Start-Sleep -Milliseconds 150
            }
        }
    }
}

$result = [pscustomobject]@{
    Host = $hostName; GeneratedAt = [DateTimeOffset]::Now.ToString('o')
    Method = 'Resolve A/AAAA, then bounded direct curl HEAD probes pinned to each resolved address; Schannel verification enabled.'
    AttemptsPerAddressAndTimeout = $Attempts; ConnectTimeoutsSeconds = @(4, 8)
    Dns = @($dnsResults); Probes = @($probes)
    Note = 'curl connect timeout covers connection establishment including TLS handshake; HTTP status is independent of TLS success.'
}
$destination = Join-Path (Get-Location) $OutputPath
$parent = Split-Path -Parent $destination
New-Item -ItemType Directory -Path $parent -Force | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $destination -Encoding utf8
$result | ConvertTo-Json -Depth 8
