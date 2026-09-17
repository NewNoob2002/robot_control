# P10.3 delivery evidence

See ../../P10_3_DELIVERY_BASELINE.md for scope and review.

- evidence-manifest.json:1473 original P10.3 evidence files, including failures.
- source-verification.json:109 current compiled inputs match accepted91d3ce99.
- focused-debug.xml / focused-sanitizer.xml:5/5 each, no skips.
- owner-vcan.xml:rebuilt single-owner ControlLoop integration1/1, no skips.
- Remote commit/run identity is recorded after push; none is inferred from local tests.

No physical run was performed for delivery. The source/test/evidence diff is
committed together because real-executable tests consume the archived analyzers.
The top-level local .clang-tidy remains outside this change.
