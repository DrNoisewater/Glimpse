param(
    [ValidateRange(1,3600)][int]$Seconds = 30,
    [string]$Output = "glimpse-performance.csv"
)
$ErrorActionPreference = 'Stop'
$target = Get-Process -Name Glimpse -ErrorAction Stop | Select-Object -First 1
$logicalProcessors = [Environment]::ProcessorCount
$samples = [System.Collections.Generic.List[object]]::new()
$previousCpu = $target.TotalProcessorTime.TotalSeconds
$clock = [System.Diagnostics.Stopwatch]::StartNew()
$previousTime = 0.0
for ($i = 0; $i -lt $Seconds; $i++) {
    Start-Sleep -Seconds 1
    $target.Refresh()
    if ($target.HasExited) { break }
    $now = $clock.Elapsed.TotalSeconds
    $cpu = $target.TotalProcessorTime.TotalSeconds
    $gpu = $null
    try {
        $engines = (Get-Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction Stop).CounterSamples |
            Where-Object { $_.InstanceName -like "pid_$($target.Id)_*" }
        if ($engines) {
            $gpu = ($engines | ForEach-Object { "$($_.InstanceName)=$([math]::Round($_.CookedValue,2))" }) -join ';'
        }
    } catch { $gpu = 'Unavailable' }
    $samples.Add([pscustomobject]@{
        ElapsedSeconds = [math]::Round($now,3)
        ProcessId = $target.Id
        WorkingSetMB = [math]::Round($target.WorkingSet64 / 1MB,2)
        PrivateBytesMB = [math]::Round($target.PrivateMemorySize64 / 1MB,2)
        CPUPercentOfMachine = [math]::Round(100 * ($cpu-$previousCpu) / ($now-$previousTime) / $logicalProcessors,2)
        GPUCounters = $gpu
    })
    $previousCpu = $cpu
    $previousTime = $now
}
$samples | Export-Csv -LiteralPath $Output -NoTypeInformation -Encoding UTF8
$samples | Measure-Object WorkingSetMB,PrivateBytesMB,CPUPercentOfMachine -Average -Maximum
Write-Host "Saved $($samples.Count) samples to $Output. Compare with the capture's .metrics.txt sidecar."
