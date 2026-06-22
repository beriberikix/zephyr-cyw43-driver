# Compliance checklist — vs the current Zephyr contribution guidelines

Source: https://docs.zephyrproject.org/latest/contribute/guidelines.html (read on
submission day — re-check, it changes). Status of our material against each rule.

| Requirement | Status / action |
|---|---|
| **DCO `Signed-off-by`** with legal name (`git commit -s`) | ⛔ AI cannot add it. **You** sign every upstream commit. Our private-fork commits are NOT signed off. |
| **"AI agents must not add Signed-off-by"** | ✅ Honored — no prepared commit text contains `Signed-off-by`. |
| **`Assisted-by: [Tool]:[Model]`** for AI-assisted code | ✅ Included in every prepared commit message (`Assisted-by: Claude Opus 4.8 <noreply@anthropic.com>`). |
| **Drop our private trailers** (`Co-Authored-By: Claude…`, `Claude-Session:`) | ⚠️ Action: do not carry these into upstream commits. (They are only on the `whd-port` fork history; upstream commits are freshly authored per Track A/B.) |
| **Commit title `area: summary`, ≤ 72 chars** | ✅ e.g. `drivers: wifi: airoc: free undersized net_buf in host_buffer_get`. |
| **Commit body present, ≤ 75 chars/line, what/why/tested** | ✅ Prepared messages are wrapped and state what/why/how-tested. |
| **`./scripts/ci/check_compliance.py -c <range>`** passes | ⚠️ Action: run locally per PR before pushing (broader than the `checkpatch` we ran during dev). |
| **RFC issue + community conversation before large features** | ✅ Track 0 (`RFC.md`) + a Discord announce in `#raspberrypi`/`#infineon` (the vendor channels; Discord is the active channel in practice, the guideline names the `devel@` list which is optional here). The coex feature is the "large feature"; the bug fixes (Track A/B) don't need it. |
| **Treewide change → AWG vote** | ✅ N/A — no Zephyr API/coding-practice change across the tree. (A new driver/feature is not "treewide.") |
| **External code: `Origin:/License:/URL:/commit:` block** | ⚠️ Applies to the module (Track C): `cybt` (BSD-3-Clause, pico-sdk), the BT-fw blob. The Track-A fix is `Origin: Original` (Apache) — no block needed. |
| **Non-Apache external component → governing-board approval** | ⛔ The **BT firmware blob** is the gated item. Raise via the RFC; do not push it upstream until the blob/governing-board path is agreed. `cybt` is BSD-3 (compatible, documentable). |
| **SPDX header + real copyright holder on new files** | ⚠️ `src/whd/whd_bt_glue.c` etc. carry a placeholder `Copyright (c) 2026`. Set a real holder (you / Beechwoods) before publishing the module. Track-A touches an existing file (no new header). |
| **Branch off `upstream/main`; PR targets `main`; `Fixes #<issue>`** | ✅ Encoded in `STEPS.md` / `track-A`. |
| **MAINTAINERS auto-assign; build/`twister` test expected for new code** | ⚠️ Track A is a small bug fix (on-board build+run noted in the body; no twister needed). The module/feature (Track C) should add at least a board build test when it targets mainline. |
| **Company authorship / CLA** | ⚠️ The georgerobotics driver carries **Beechwoods Software** copyright. Anything derived from it may need Beechwoods authorship/sign-off. **Your call** — confirm before submitting derived work. |

## Per-track readiness
- **Track A (airoc leak fix):** ✅ ready to submit now (after your `-s` + `check_compliance`).
- **Track B1 (cybt re-read):** ✅ ready (target repo's own DCO/CONTRIBUTING applies).
- **Track B2 (NVRAM btc_mode):** ⚠️ issue-first; vendor-asset/regulatory caution.
- **Track C (module + coex patches):** ⛔ blocked on RFC direction + blob path + SPDX/copyright + the verification re-soak.

## The one-line summary
Bug fixes (Track A, B1) are compliant and submittable now with your sign-off; the
coexistence *feature* (Track C) and the BT firmware blob are gated on the RFC and the
governing-board/blob process — which is exactly why the RFC goes first.
