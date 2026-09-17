# F5 repeated resynchronization and stable recovery

Phase2 parser rejection is retained while Source remains revoked and resets both
stable input qualification and nonneutral-challenge progress. Fresh healthy
Disabled/neutral input,CH6 released,received timestamps spanning>=1s are required
before each uart_reconnected cue. Later degradation invalidates the old prompt;
runner forwards repeated transitions in order.45s deadline,120s total,5s fault
hold and hard guards unchanged. No Parser/Reader/Source production changes.

Old implementation fails the new stable-window regression. Two focused vcan/PTY
bounce cases pass,including errors470ms after initial recovery and after a prior
ready cue. Full Debug46/46 and ASan/UBSan46/46 pass without skips. Scoped static,
locked cross,qualification ELF and109-source-input snapshot checks pass. Independent
oracle rejects bad/stale/nonneutral/short stable claims and preserved A3 evidence.
Artifact91d3ce99c06b4f96148a1e8cc86412459d53fe1bce88a91ec41f1d64dc33430c
passed target smoke and authorized A4 HIL. See zero_uart_a4/acceptance.json.
