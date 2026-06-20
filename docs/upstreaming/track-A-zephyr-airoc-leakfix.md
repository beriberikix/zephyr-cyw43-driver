# Track A — standalone Zephyr-main PR: AIROC `net_buf` leak fix

This is the one piece that is **mergeable to `zephyrproject-rtos/zephyr` today**:
self-justifying, Apache, `Origin: Original`, **zero BT/coex dependency**. Land it
first — it is the low-risk, credibility-building contribution and is independent of
the RFC outcome.

## The bug
`airoc_wifi_host_buffer_get()` in `drivers/wifi/infineon/airoc_wifi.c` allocates a
`net_buf` from `airoc_pool`, then:

```c
buf = net_buf_alloc_len(&airoc_pool, size, K_NO_WAIT);
if ((buf == NULL) || (buf->size < size)) {
    return WHD_BUFFER_ALLOC_FAIL;
}
```

If `net_buf_alloc_len()` returns a non-NULL buffer that is **smaller than
requested** (`buf->size < size`), the function returns failure **without releasing
`buf`** — it leaks from the fixed pool. Over time this can exhaust `airoc_pool`.

## The fix (leak-fix ONLY — exclude the coexistence buffer-reserve)
> NOTE: the repo's `patches/airoc_wifi_bt_backplane_buffer_reserve.patch` bundles
> this leak fix **with** a BT-coexistence buffer-reserve (`airoc_pool_inflight`,
> `AIROC_POOL_BT_RESERVE`, the `airoc_mgmt_send` throttle). For this upstream PR,
> include **only** the hunk below. The coexistence reserve depends on the BT-over-WHD
> feature and goes through the RFC (Track C), not this PR.

Apply exactly this change in `airoc_wifi_host_buffer_get()`:

```diff
 	buf = net_buf_alloc_len(&airoc_pool, size, K_NO_WAIT);
-	if ((buf == NULL) || (buf->size < size)) {
+	if (buf == NULL) {
+		return WHD_BUFFER_ALLOC_FAIL;
+	}
+	if (buf->size < size) {
+		/* net_buf_alloc_len() returned a buffer smaller than requested;
+		 * release it before failing or it leaks from airoc_pool.
+		 */
+		net_buf_unref(buf);
 		return WHD_BUFFER_ALLOC_FAIL;
 	}
 	*buffer = buf;
```

(Do **not** add `atomic_inc(&airoc_pool_inflight)` — that line is part of the coex
reserve, not the leak fix.) `net_buf_unref()` is already available via the included
`<zephyr/net_buf.h>`; no new include needed.

## Prepared commit message (paste as-is; YOU add the sign-off)
Title and body comply with the Zephyr guidelines (title ≤ 72 chars; body ≤ 75/line;
`Assisted-by:` for AI assistance; **no** `Signed-off-by` — `git commit -s` adds yours;
**no** `Co-Authored-By`/`Claude-Session`).

```
drivers: wifi: airoc: free undersized net_buf in host_buffer_get

airoc_wifi_host_buffer_get() returns WHD_BUFFER_ALLOC_FAIL when
net_buf_alloc_len() yields a buffer smaller than the requested size, but
it does so without releasing that buffer, leaking it from the fixed
airoc_pool. Release the undersized buffer with net_buf_unref() before
returning the failure.

Found while characterizing airoc_pool exhaustion under sustained traffic
on rpi_pico2/rp2350a/m33/w (CYW43439). Build- and runtime-tested on that
board: WiFi associate + DHCP + sustained ping with no pool exhaustion
from this path.

Assisted-by: Claude Opus 4.8 <noreply@anthropic.com>
```

> When you run `git commit -s`, Git appends `Signed-off-by: Your Name
> <your@email>`. Keep `Assisted-by:` above it. An AI must not add the
> `Signed-off-by` line — that certification is yours.

## Step-by-step (human)
Assumes you have a GitHub fork of `zephyrproject-rtos/zephyr` and a Zephyr west
workspace. Adjust the workspace path.

```sh
# 0. In your Zephyr west workspace, make sure the zephyr repo has your fork + upstream
cd <west-workspace>/zephyr
git remote -v                      # expect: origin=your fork, upstream=zephyrproject-rtos/zephyr
# if missing:
#   git remote rename origin upstream   (if origin currently points at upstream)
#   git remote add origin git@github.com:<you>/zephyr.git

# 1. Fresh topic branch off upstream/main
git fetch upstream
git checkout -b airoc-free-undersized-netbuf upstream/main

# 2. Apply the leak-fix hunk above by hand in
#    drivers/wifi/infineon/airoc_wifi.c (function airoc_wifi_host_buffer_get).
#    (Do not use the repo's combined patch; only the hunk above.)
$EDITOR drivers/wifi/infineon/airoc_wifi.c

# 3. Build-check on the board (sanity; the AIROC WiFi sample or any AIROC board)
west build -p always -b rpi_pico2/rp2350a/m33/w samples/net/wifi   # or your app

# 4. Commit with YOUR sign-off + the prepared message
git add drivers/wifi/infineon/airoc_wifi.c
git commit -s            # paste the prepared message; -s adds your Signed-off-by

# 5. Compliance (must pass; broader than checkpatch)
./scripts/ci/check_compliance.py -c upstream/main..HEAD

# 6. Push + open the PR against zephyrproject-rtos/zephyr:main
git push origin airoc-free-undersized-netbuf
#   then "Compare & pull request" on GitHub; base = zephyrproject-rtos/zephyr main.
#   In the PR body, link the RFC issue (Track 0) for context, e.g. "Found via #<RFC#>".
```

## Notes
- This is a bug fix, not a feature, so it does **not** need to wait on the RFC.
- Reviewers auto-assign via `MAINTAINERS.yml` (`drivers/wifi/infineon`).
- No test is strictly required for a small driver bug fix, but mention the on-board
  build + run you did in the commit body (done above).
- If `check_compliance.py` flags anything, fix and `git commit --amend -s`, then
  re-run before pushing.
