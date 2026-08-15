# Air Sentinel — a gas detector that reports its own blind spots

**Category:** Electronics / Instrumentation / Environmental Science
**Hardware:** Arduino Uno · MQ-135 semiconductor gas sensor · 16×2 I²C LCD · piezo alarm
**Software:** Arduino C++ firmware · browser dashboard over the Web Serial API

---

## The problem

Semiconductor gas sensors of the MQ family are the standard component in student and hobby gas-detection projects. Almost every such project reports a concentration in parts per million for a named gas — "LPG: 420 ppm", "CO: 85 ppm".

Those numbers are not measurements. They are the output of a curve fit applied far outside the range where the curve was characterised, attributed to a gas the sensor has no way to identify.

An MQ sensor is a heated tin-dioxide (SnO₂) bead. Its electrical resistance falls when *any* reducing gas adsorbs onto the surface. It produces **one number** — a resistance. Not a spectrum, not a fingerprint. Liquefied petroleum gas at 500 ppm, ethanol vapour at 200 ppm, and cigarette smoke can all produce an identical reading. The sensor cannot distinguish them, and neither can any software running on top of it.

This project asks a different question from the usual one. Not *"how do we make the sensor report a number?"* but *"what is this sensor actually entitled to claim, and how do we build an instrument that reports exactly that and nothing more?"*

---

## What was built

A working combustible-gas alarm with a live analysis dashboard, in which every displayed quantity is traceable to what the hardware can actually support.

**Detector (autonomous).** An Arduino Uno reads the MQ-135's analog output, converts it to sensing resistance Rs, and compares it against R₀ — the same resistance measured in clean air. The ratio Rs/R₀ is the single physical measurement the system makes. A 16×2 LCD displays it, a piezo alarm latches on threshold breach with debounce and hold, and the whole assembly runs from a 5 V supply with no computer attached.

**Analysis station.** A browser dashboard reads the detector over USB using the Web Serial API and renders the interpretation layer: per-gas concentration estimates, hazard classification, history, and an explicit statement of measurement validity. The page needs no server, no network, and no cloud service.

## What is novel

Three design decisions distinguish this from a conventional MQ project.

**1. Range clamping.** The datasheet characterises each gas curve only across a limited band. Outside that band the power-law fit is extrapolation with no physical meaning — evaluated at a low resistance ratio, an unclamped fit returns concentrations orders of magnitude beyond anything the sensor was characterised for, and that fabricated figure would dominate any naive alarm. Every gas therefore carries an explicit validity band, readings are clamped to it, and out-of-band values are flagged rather than reported as measurements. The MQ-135's carbon dioxide curve makes the point concretely: its danger threshold of 5,000 ppm lies above the sensor's own 1,000 ppm ceiling, so the instrument reports that the threshold is unreachable rather than pretending to cross it.

**2. Resolvability gating.** A gas whose hazard threshold lies *below* the sensor's own detection floor is excluded from alarm logic entirely and labelled unresolvable. For the MQ-135 this applies to benzene, which is a carcinogen with an occupational action level of 1 ppm against a sensor floor of 10 ppm: the instrument could only detect it once exposure was already an order of magnitude over the limit, so the row is displayed for context and can never raise an alarm. The same principle exposes a larger gap by its absence — the MQ-135 has no carbon monoxide response curve at all, and the interface states this rather than leaving a reader to assume a general-purpose air-quality sensor covers the most lethal common indoor gas.

**3. Separation of measurement from inference.** The dashboard presents Rs/R₀ as the measurement and each per-gas concentration as one conditional interpretation of it — *"if this were LPG, it would be this much"* — stating on the page that at most one interpretation can be true and the sensor does not know which. Raw instrument values and derived values are typographically distinguished so the boundary is visible at a glance.

## Air quality and occupancy safety

The system computes a sensor-derived combustible-gas index and, separately, presents the CPCB Air Quality Index reference scale. It states plainly that the two are not the same thing: the CPCB AQI requires PM2.5, PM10, ozone, nitrogen dioxide, sulphur dioxide, carbon monoxide, ammonia and lead measured by reference-grade instruments over eight- and twenty-four-hour windows, none of which this sensor provides.

An occupancy assessment answers "is it safe to be in this space?" using published exposure limits — the ten-percent-of-lower-explosive-limit industrial alarm convention for combustibles, and NIOSH/ACGIH time-weighted-average limits with IDLH ceilings for toxics — together with a permissible exposure time derived from the standard TWA relation. The assessment carries a permanent qualification: a "safe" verdict means only that nothing this sensor can detect exceeds threshold, and the device is blind to particulates, ozone, oxygen depletion and low-level carbon monoxide.

## Method

R₀ is derived by averaging 64 samples in clean outdoor air and dividing by the datasheet's clean-air ratio, then stored in EEPROM so it survives power cycling. The sensor requires 24 to 48 hours of continuous burn-in before its baseline stabilises, and a 180-second heater warm-up on each cold start; readings before either are treated as invalid. Concentration follows the power law ppm = a·(Rs/R₀)^b, with coefficients obtained by least-squares fitting to the published datasheet curves — a provenance stated in the interface, because it bounds the accuracy claim at order-of-magnitude.

Cross-sensitivity is demonstrated directly rather than described: exposing the sensor to butane and then to isopropyl alcohol produces comparable responses that the instrument reports identically, confirming the identification limit experimentally.

## Result

A functioning detector that alarms correctly on combustible gas, and an interface in which no displayed number exceeds what the hardware can justify. The project's contribution is not a better sensor but a more honest instrument built around an ordinary one — and a demonstration that stating an instrument's limits is more useful than concealing them.

## Extension

Gas identification requires breaking the single-measurement bottleneck. The designed path is an array of four to six differently-doped MQ sensors read simultaneously: each has a distinct sensitivity ratio to each gas, so the *pattern* across the array is gas-specific in a way no single element is, and a k-nearest-neighbour or small neural classifier trained on labelled exposures can recover identity. Cheaper partial alternatives include thermal cycling of a single element, which exploits the temperature dependence of selectivity to extract two semi-independent readings from one sensor.

---

*This is a demonstration and teaching instrument. It is not a certified life-safety device, and the project documentation says so wherever a reading is displayed.*
