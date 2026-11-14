# Prism speech

Sparking Instinct uses the official Windows x64 Prism 0.18.3 release. Verify `dynamic/release/bin/prism.dll` against the lock file and place it beside the native mod DLL. The package includes the corresponding notices. [Dependency lock](prism-lock.json) · [Prism release](https://github.com/ethindp/prism/releases/tag/v0.18.3)

Link `sparking_prism` and start the speech worker with the absolute DLL path. Send UTF-8 text to `sparking::speech::say`. An interrupt replaces waiting speech. Focus loss discards pending speech, and the worker checks foreground ownership again before delivery. Call `shutdown()` only during orderly teardown outside the game tick and DLL entry point.

Enable `BUILD_TESTING` for the mailbox checks. Set `SPARKING_PRISM_TEST_DLL` to the absolute path to the pinned Prism DLL for the ABI test.
