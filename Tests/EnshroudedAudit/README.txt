Targeted Enshrouded v19 audit probes (Apache-2.0)

Obtain Vulkan-Headers and Volk from their upstream repositories or a Vulkan
SDK. From this directory, with their paths in VULKAN_HEADERS and VOLK:

  clang -O2 -I"$VULKAN_HEADERS/include" -I"$VOLK" vulkan_probe.c "$VOLK/volk.c" -o /tmp/mvk-audit-probe
  VK_DRIVER_FILES=/absolute/path/to/mvk-icd.json MVK_PERF_ICOUNT_MAX_DRAWS=128 /tmp/mvk-audit-probe /absolute/path/to/libvulkan.1.dylib

The ICD must point at the library under test. Native enumeration through the
loader needs an ICD which permits enumeration without the portability flag
(as used in the Wine comparison). Do not change a shared system ICD.

Repeat with PROBE_INDEXED=1 for indexed draws, MVK_PERF_ICOUNT_MAX_DRAWS=0
for the uncapped comparison, and MVK_PERF_HOIST_INDIRECT_COUNT=0 to disable
hoisting (the option is defined in MVKCommandBuffer.mm).

The probe records one indirect-count command with 256 real draws and an
application maximum of 1024, then reuses it six times. Expected observations:
  nivogli snapshot, initial cap 128: 128 128 128 128 128 128 (FAIL)
  corrected, initial cap 128:        128 128 256 256 256 256 (PASS)
  uncapped:                         256 256 256 256 256 256 (PASS)
The precise adaptation cadence depends on driver implementation. PASS only
asserts the last submission reached 256 and the 64-step dependent-compute
readback passed. It does not assert the initial capped draws were correct.
No secondary-command-buffer or subpass-specific GPU regression is included.

The shaders are small test shaders, not game extracts. Their embedded uint32_t
SPIR-V arrays can be regenerated using glslangValidator -V and array conversion.
For Windows/Wine compile vulkan_probe.c and Volk using MinGW with -static;
the executable loads vulkan-1.dll and takes no loader argument. Use a scratch
Wine prefix, not the game prefix.

hdr_present_probe.mm is a small native Metal presentation experiment:
  clang++ -std=c++17 -O2 -fobjc-arc -framework Cocoa -framework Metal -framework QuartzCore hdr_present_probe.mm -o /tmp/hdr-present-probe
It takes no arguments and opens a visible window. PROBE_HDR_ONLY=1 selects
the paired HDR-only comparison; the default covers all eight combinations.
It is not a Vulkan test or a reproduction of Enshrouded's rendering load.
Its original results are summarized with limits in Docs/Enshrouded-v19-audit.txt.
