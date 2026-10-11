# Existing AIDL Health supply selection

Apply `0001-health-select-physical-charger-supplies.patch` in
`hardware/interfaces` on the matching LineageOS branch. It is already applied
in this workspace. It changes the existing default Health service, including
its recovery build; it introduces no AIDL interface, service or binary.

Apply `0002-health-honor-supply-exclusions-on-rescan.patch` in `system/core`.
It is also applied here. BatteryMonitor's periodic rescan previously ignored
the exclusions honored by init(), reintroducing TCPM's advertised capability.
The rescan now honors the same existing configuration as initialization.

The existing `healthd_config::ignorePowerSupplyNames` selection comes from
`ro.vendor.health.ignore_supply_names` (comma-separated exact sysfs names).
The Mi A3 ROM supplies the duplicate TCPM source name in power/health.prop. With
an empty property, other devices retain their existing behavior. No kernel
TCPM capability is falsified. Empty exclusion lists retain existing behavior.

Rebuild the complete ROM, including android.hardware.health-service.example.
The existing recovery prop.default recipe concatenates vendor build.prop,
so the same power/health.prop selection is included in recovery (build/make/core/Makefile).
The physical PMI632 charger provides measured VBUS and its input-current
limit. Android's rapid/normal label describes that advertised charging
capability; taper at high SOC can still draw substantially less power.

This is a new local adaptation. The AOSP source copyright is preserved.
Hardware and SELinux validation of the new candidate are pending.
