# STEPS — everything YOU (the human) need to do, in order

The AI prepared all text/diffs/commands but **cannot** do outward actions or certify
the DCO. You do these. Each commit you make: `git commit -s` (adds your real
`Signed-off-by`), keep the prepared `Assisted-by:` line, and **remove** any
`Co-Authored-By: Claude…` / `Claude-Session:` trailers (those were for the private
fork only).

Cross-references: `RFC.md`, `track-A-zephyr-airoc-leakfix.md`,
`track-B-hal-submissions.md`, `track-C-module.md`, `compliance-checklist.md`.

---

## 0. One-time prep
- [ ] Confirm your Git identity is your **legal name** + the email you sign with:
      `git config user.name`, `git config user.email`.
- [ ] Confirm/locate forks: `zephyrproject-rtos/zephyr`, `raspberrypi/pico-sdk`
      (and optionally `zephyrproject-rtos/hal_rpi_pico`, `zephyrproject-rtos/hal_infineon`).
- [ ] (If contributing on behalf of Beechwoods Software) confirm any
      company authorship/CLA expectations for work derived from the
      Beechwoods georgerobotics driver — see `compliance-checklist.md`.

## 1. Track 0 — RFC issue FIRST (do before any feature code)
- [ ] Open a new issue at https://github.com/zephyrproject-rtos/zephyr/issues/new,
      label **RFC**. Title:
      `RFC: WiFi+BT coexistence over the WHD gSPI bus for CYW43439 (Pico 2 W)`.
- [ ] Paste the body of `docs/upstreaming/RFC.md` (everything below its title;
      skip the leading HTML comment).
- [ ] Post a short heads-up to **devel@lists.zephyrproject.org** linking the issue
      (the guidelines ask for a conversation before large features).
- [ ] Note the issue number as `<RFC#>` and use it in later PRs.
- [ ] Wait for maintainer direction on: BT-HCI transport home, coex-arbitration
      home, the firmware-blob path. (Tracks A/B can proceed in parallel.)

## 2. Track A — the clean Zephyr-main leak-fix PR (mergeable now)
Follow `track-A-zephyr-airoc-leakfix.md` exactly. Summary:
- [ ] `cd <west-workspace>/zephyr`; branch `airoc-free-undersized-netbuf` off `upstream/main`.
- [ ] Apply **only** the leak-fix hunk to `drivers/wifi/infineon/airoc_wifi.c`
      (NOT the coex buffer-reserve).
- [ ] `west build -p always -b rpi_pico2/rp2350a/m33/w …` (sanity build).
- [ ] `git commit -s` with the prepared message (keep `Assisted-by:`).
- [ ] `./scripts/ci/check_compliance.py -c upstream/main..HEAD` → green.
- [ ] `git push origin …`; open PR vs `zephyrproject-rtos/zephyr:main`; reference `#<RFC#>`.
- [ ] Watch CI; address review with `git commit --amend -s` + `git push --force`.

## 3. Track B — HAL/vendor submissions (parallel with the RFC)
Follow `track-B-hal-submissions.md`.
- [ ] **B1 cybt re-read** → PR to `raspberrypi/pico-sdk` (and/or `hal_rpi_pico`),
      branch `cybt-reread-corrupt-index`, prepared message, `git commit -s`,
      check the target repo's CONTRIBUTING/PR template.
- [ ] **B2 btc_mode NVRAM** → open an **issue** on `zephyrproject-rtos/hal_infineon`
      first (it's a vendor RF asset). Ask: change the asset vs a board-level override.
      Attach `docs/artifacts/w4_btx_geo_vs_whd_discriminator_20260620.log`. Submit the
      change where the maintainers/Infineon direct.

## 4. Track C — the out-of-tree coex module (after RFC direction)
Follow `track-C-module.md` + `docs/whd-standalone-module-plan.md`.
- [ ] Ask the AI to scaffold the module repo (compat header, west.yml/module.yml,
      README/PROVENANCE) per the standalone-module plan.
- [ ] **Build + re-run the coexistence soak** on the module to prove parity with the
      2 h / 0-fault result before publishing.
- [ ] `git init` / create + push the new GitHub repo (your action).
- [ ] Link the module from the RFC as the reference implementation.

## 5. Ongoing
- [ ] Keep each PR rebased on its `upstream/main`; re-request review after changes.
- [ ] As the RFC concludes, move the bus-lock / buffer-reserve patches to whatever
      home maintainers chose (mainline coex layer vs the module).

---

### Quick "do not" list (compliance)
- Do **not** let the AI add `Signed-off-by` — only you can (`git commit -s`).
- Do **not** keep `Co-Authored-By: Claude…` / `Claude-Session:` in upstream commits.
- Do **not** change the vendor NVRAM asset without maintainer/Infineon agreement.
- Do **not** bundle the coex buffer-reserve into the Track-A leak-fix PR.
- Do **not** push the BT firmware blob upstream without the blob/governing-board path.
