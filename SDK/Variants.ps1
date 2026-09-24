# Shared packaging/build matrix. Runtime selection is independent of HTN instrumentation.
$HTNVariants = @(foreach ($linkage in @('Static', 'Dynamic')) {
    foreach ($crt in @('Debug', 'Release')) {
        foreach ($instrumentation in @('Plain', 'Instrumented')) {
            $instrumented = $instrumentation -eq 'Instrumented'
            $defines = @()
            if ($crt -eq 'Debug') { $defines += @('HTN_DEBUG', '_DEBUG') } else { $defines += @('HTN_RELEASE', 'NDEBUG') }
            if ($instrumented) { $defines += @('HTN_ENABLE_LOGGING', 'HTN_VALIDATE_DOMAIN', 'HTN_DEBUG_DECOMPOSITION') }
            $runtime = 'MultiThreaded'
            if ($crt -eq 'Debug') { $runtime += 'Debug' }
            if ($linkage -eq 'Dynamic') { $runtime += 'DLL' }
            [ordered]@{
                id = "$linkage$crt$instrumentation"
                runtime_linkage = $linkage.ToLowerInvariant()
                runtime_configuration = $crt
                instrumentation = $instrumented
                msvc_runtime_library = $runtime
                iterator_debug_level = $(if ($crt -eq 'Debug') { 2 } else { 0 })
                optimization = $(if ($crt -eq 'Debug') { 'off' } else { 'full' })
                symbols = $true
                defines = $defines
            }
        }
    }
})
