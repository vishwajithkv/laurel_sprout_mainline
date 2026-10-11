# Mi A3 6.18 source repositories

Organization: [Mi A3 Mainline Development](https://github.com/laurel-sprout-mainline).
The Android device repository remains under vishwajithkv.

Workspace source snapshot, 2026-10-11. Generated binaries and raw device logs
are excluded. The kernel revision below contains the implementation; later
documentation-only commits may follow it.

| Checkout path | Repository and branch | Revision |
| --- | --- | --- |
| `kernel/mainline/sm6125-mainline-6.18` | [laurel_sprout_mainline: mainline-6.18-split](https://github.com/laurel-sprout-mainline/laurel_sprout_mainline/tree/mainline-6.18-split) | `ea8344571b4499379b93463edb28ad49cdfcc7f6` |
| `kernel/mainline/sm6125-mainline-6.18-devicetrees` | [kernel_xiaomi_laurel_sprout-devicetrees: mainline-6.18](https://github.com/laurel-sprout-mainline/kernel_xiaomi_laurel_sprout-devicetrees/tree/mainline-6.18) | `aa1e2bbe0a5dcb181eb09687e772e85540a31cf4` |
| `kernel/mainline/sm6125-mainline-6.18-modules` | [kernel_xiaomi_laurel_sprout-modules: mainline-6.18](https://github.com/laurel-sprout-mainline/kernel_xiaomi_laurel_sprout-modules/tree/mainline-6.18) | `412259e19cfbb3baa05c5bd543ca073df09a5abc` |
| `device/xiaomi/laurel_sprout` | [android_device_xiaomi_laurel_sprout: lineage-23.2-6.18-split](https://github.com/vishwajithkv/android_device_xiaomi_laurel_sprout/tree/lineage-23.2-6.18-split) | `e6db0ccccba3a8c132280d0ca3ede8c68da14d2d` |
| `hardware/mainline/qcom` | [android_hardware_mainline_qcom: lineage-24.0](https://github.com/laurel-sprout-mainline/android_hardware_mainline_qcom/tree/lineage-24.0) | `53b62da1649e6e1d1fd33fa545c897935b5459b9` |
| `hardware/mainline/common` | [android_hardware_mainline_common: lineage-23.2-6.18](https://github.com/laurel-sprout-mainline/android_hardware_mainline_common/tree/lineage-23.2-6.18) | `eb5240c1fe18d2274983d40bcfb439bd988cd920` |
| `bootable/recovery` | [android_bootable_recovery: lineage-23.2-6.18](https://github.com/laurel-sprout-mainline/android_bootable_recovery/tree/lineage-23.2-6.18) | `3e07dbfeafb507457eb953b3337038cb10149c51` |
| `external/drm_hwcomposer-upstream` | [android_external_drm_hwcomposer-upstream: lineage-23.2-6.18](https://github.com/laurel-sprout-mainline/android_external_drm_hwcomposer-upstream/tree/lineage-23.2-6.18) | `bf87e20abee7731f59a2df1b7fc9e52201af4fbb` |
| `external/mesa` | [android_external_mesa: lineage-23.2-6.18](https://github.com/laurel-sprout-mainline/android_external_mesa/tree/lineage-23.2-6.18) | `37fd285728f414c931438e41d7c6f0c51674931d` |
| `external/zstd` | [android_external_zstd: lineage-23.2-6.18](https://github.com/laurel-sprout-mainline/android_external_zstd/tree/lineage-23.2-6.18) | `19cce4b5991ab5baf3eed6c5c42a78f4e0ebfa66` |
| `hardware/interfaces` | [android_hardware_interfaces: lineage-23.2-6.18](https://github.com/laurel-sprout-mainline/android_hardware_interfaces/tree/lineage-23.2-6.18) | `f6ad82b8fac31d68bbd55625b6dcde23e5fe1071` |
| `packages/apps/Settings` | [android_packages_apps_Settings: lineage-23.2-6.18](https://github.com/laurel-sprout-mainline/android_packages_apps_Settings/tree/lineage-23.2-6.18) | `a4c73561064f88b410fcd47c39e23c16d0d08849` |
| `packages/modules/Connectivity` | [android_packages_modules_Connectivity: lineage-23.2-6.18](https://github.com/laurel-sprout-mainline/android_packages_modules_Connectivity/tree/lineage-23.2-6.18) | `70fdb0d2f606d0af2f9b40d83dc020cfa996f881` |
| `system/connectivity/wificond` | [android_system_connectivity_wificond: lineage-23.2-6.18](https://github.com/laurel-sprout-mainline/android_system_connectivity_wificond/tree/lineage-23.2-6.18) | `dc55085ed3fbdf755cbf784c4d4dc9445e7be799` |

`system/core` remains the upstream checkout with two local changes carried
as patches in this kernel repository:

- `rom-patches/system-core/0001-init-recognize-legacy-normal-boot.patch`
- `rom-patches/health/0002-health-honor-supply-exclusions-on-rescan.patch`

Apply these to the matching LineageOS base when recreating the workspace.
The existing ROM-patch copies also preserve the shared Android integration
changes alongside the forks. PMIC measurements and outstanding limitations
are recorded in PMIC_BUILD_33.md.
