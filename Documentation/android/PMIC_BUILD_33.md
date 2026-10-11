# Build 33 PMIC check, 2026-10-11

Read-only ADB inspection on laptop USB. Kernel 6.18.32 dirty build #33.
No adapter negotiation commands, forced SOC, reboot or service changes.
Samples are sequential, not atomic snapshots; raw captures remain local.
Device wall time is wrong; use boot-relative time and sample order.

Confirmed:

- PM6125 RTC binds as rtc0. Boot restored SDAM SOC 97%, age 20 seconds.
- Estimator progresses 97.03 -> 97.15 -> 97.26 -> 97.36 -> 97.45 -> 97.56%.
  The final BatteryService capture reports integer level 98. It is not stuck.
- Stable laptop samples: input about 5.02 V, 0.45-0.47 A; battery about
  4.41 V, positive 0.23-0.25 A, temperature 33.3 C, health Good. Net battery
  current is positive in these samples. Input power about 2.3 W does not
  imply all that power goes into the battery.
- Source supply exclusion still works: BatteryService receives 500 mA,
  not TCPM's 3 A source advertisement. Charging slowly is consistent here.

Remaining concerns:

- Primary USB type remains Unknown. A boot log reports QC negotiation
  unavailable and 5 V fallback. Laptop data cannot validate wall charging.
- Brief primary current_max=0 / status changes occur, with at least one
  initial negative battery-current sample. Later stable charging does not
  prove these transient events are resolved. Sequential differing primary
  and gauge status readings are not evidence of a permanently inconsistent
  state; synchronized raw state is needed to identify the transition.
- current_max reads DCDC_ICL_STATUS (settled input limit), not a fixed adapter
  rating. The UI label uses that reported current and voltage. Near-full
  taper can coexist with a lower reported limit, but SOC 97 alone cannot
  establish the reason the wall adapter showed Slowly.
- SMB1355 still fails ID probe with -ENXIO. No validated 18 W adapter result.
- Reported full capacity 3172 mAh / design 4040 mAh gives about 78.5%; cycles
  971. These are retained history/model values, not independently verified
  current state of health. Health Good means no current reported battery
  fault, not 100% lifetime capacity.

Next adapter validation should capture charger classification, voltage, input
limit/current, battery current and temperature during the wall session. Use
wireless ADB if already available or reconnect laptop USB afterward to retrieve
retained logs. Do not infer adapter support from the lock-screen label alone.

## Wall-adapter policy inspection

LineageOS lineage-23.2 revision 77d2912bc00eda19182a95a24bbe23543c792814
Laurel Sunwoda, Feimaotui and default battery profiles contain no
qcom,step-chg-ranges or qcom,soc-based-step-chg. The generic downstream
step-charging code requires profile ranges. Current 6.18 charger policy has
no explicit 90% SOC derating gate. Both use a 4.4 V charge target; hardware
constant-voltage taper reduces current near full charge. The observed
near-full slowdown is consistent with taper, but laptop readings cannot
validate wall-adapter QC negotiation. Temperature derating is independent.
