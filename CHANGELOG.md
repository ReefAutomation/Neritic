## 2.1.0 (2026-10-15)

### Features

* **ios:** add native controller app ([7948e67](https://github.com/ReefAutomation/Neritic/commit/7948e6784a6ecc88fa740b76f423b0299097feed))

### Bug Fixes

* emit canonical v-prefixed firmware version ([a72bb39](https://github.com/ReefAutomation/Neritic/commit/a72bb3946e8db6940a3f7feb731354c3d217a6d4))

## 2.0.0 (2026-10-13)

### ⚠ BREAKING CHANGES

* **tzdata:** the dstEnabled setting is removed - daylight saving is always
applied from the selected zone's rules. /api/timezones previously returned an
array of plain name strings.

### Features

* **effects:** add reef effects and staged LED transitions ([5d509bb](https://github.com/ReefAutomation/Neritic/commit/5d509bb27d70730950bddfcccff8c463b2157d5e))
* **locate:** add a self-hosted GPS and timezone page ([caf8cff](https://github.com/ReefAutomation/Neritic/commit/caf8cff8f9ec69c92ec5db340c252f6dd6703d0f))
* **tzdata:** generate timezone data from IANA and serve GMT offsets ([cae73a3](https://github.com/ReefAutomation/Neritic/commit/cae73a3f250a82ecca8623e4acc0d8b97978ca51))
* **web:** add a custom RGBW color picker and fix color updates ([335765e](https://github.com/ReefAutomation/Neritic/commit/335765eec92fdbc7e2e51526d6f3e2fbbbc97ae2))
* **web:** smooth the LED preview by suspending the WebSocket per tab ([31753ff](https://github.com/ReefAutomation/Neritic/commit/31753ffa7ca5d3068769d2b7731d11c0a223a816))

### Documentation

* add a visualization image ([e76ffda](https://github.com/ReefAutomation/Neritic/commit/e76ffda0af7403117a5c3e4cd79ff9797945f474))

### Continuous Integration

* **release:** generate release notes for releases ([63df90e](https://github.com/ReefAutomation/Neritic/commit/63df90e71f2382ee8649303a22d2e9508a803bab))

### Miscellaneous Chores

* rename the project ([9bf2390](https://github.com/ReefAutomation/Neritic/commit/9bf23907a846529945180118ef9a26d665aff91d))
* **renovate:** bump package.json ranges alongside the lockfile ([be8d55c](https://github.com/ReefAutomation/Neritic/commit/be8d55c033ade8edb3918c56d87fd836dcba4dfa))

## 1.1.0 (2026-06-18)

### Code Refactoring

* **build:** split the PlatformIO config into modular sdkconfig.defaults ([1630362](https://github.com/ReefAutomation/Neritic/commit/1630362131c0463e48be58b2cea5b62abedae155))

### Continuous Integration

* update the release workflow ([1d09674](https://github.com/ReefAutomation/Neritic/commit/1d096744499203b321259b173dc49c6c34e0c305))

### Miscellaneous Chores

* **renovate:** update the Renovate configuration ([ac76e8c](https://github.com/ReefAutomation/Neritic/commit/ac76e8c4243df8fb725dc9837fcb23a1b47aa9cf))

## 1.0.4 (2026-06-12)

### Features

* **firmware:** update onboard LED support and platform configuration ([1fd9c1d](https://github.com/ReefAutomation/Neritic/commit/1fd9c1de003619c2eec43bbd024483e108e22f7c))

## 1.0.3 (2026-06-10)

### Miscellaneous Chores

* **renovate:** add renovate.json ([aee680f](https://github.com/ReefAutomation/Neritic/commit/aee680f51ef3cd151a6b2684cac7bf9aca75a0d5))

## 1.0.1 (2026-06-07)

### Features

* **effects:** refine the sunrise effect and defaults ([3e33dd0](https://github.com/ReefAutomation/Neritic/commit/3e33dd096724e4f43a1b1c32fceeee4a7c86d104))

### Continuous Integration

* **release:** update the release workflow and requirements ([e8ee301](https://github.com/ReefAutomation/Neritic/commit/e8ee30136ad26818ea7cb25999b54f76bec15ed0))

## 1.0.0 (2026-03-04)

### Features

* add network and OTA upload support ([78b7528](https://github.com/ReefAutomation/Neritic/commit/78b7528325321c5c2d1306032aafe706dc23330f))
* add timer override, dynamic effects and the display ([69d2768](https://github.com/ReefAutomation/Neritic/commit/69d276823ca3a40406e808a99f481dbcf0e51866))
* add version reporting, automatic updates and GPS support ([de7eadf](https://github.com/ReefAutomation/Neritic/commit/de7eadf50cab9faf2c2f91204fb6eae5f8ef7b99))
* **config:** add location and transition configuration ([4bfc658](https://github.com/ReefAutomation/Neritic/commit/4bfc658a62bb3acdacdcdc7a1c94bc5a5d8411ca))
* **effects:** add custom effects and colour configuration ([fa926ac](https://github.com/ReefAutomation/Neritic/commit/fa926acf252d68cc163713f8bc2fb961e776cb69))
* **effects:** add staged LED transitions ([6906fad](https://github.com/ReefAutomation/Neritic/commit/6906fadb82beaed9797faa52a6a1f35a62dd2dcf))
* **web:** add the clock, toasts and OTA confirmation ([3b6ce7b](https://github.com/ReefAutomation/Neritic/commit/3b6ce7bdb832581922940b276329f6df3489462f))
* **web:** add the configuration page and timers ([8e467aa](https://github.com/ReefAutomation/Neritic/commit/8e467aaf48acfd9e4cb817f684f0ca9c21ff6856))
* **web:** add the live bar and animated transitions ([e47c47c](https://github.com/ReefAutomation/Neritic/commit/e47c47c17a521e9bb049959baa28e201e2074cb1))

### Code Refactoring

* **firmware:** migrate to ESP-IDF with ESP32-C6 support ([b348abe](https://github.com/ReefAutomation/Neritic/commit/b348abeebc4982df9b040ed030141337a8b929ce))

### Miscellaneous Chores

* bootstrap the firmware and web UI ([4d51dba](https://github.com/ReefAutomation/Neritic/commit/4d51dba93209827943b8d60c3045a746d4a4cf72))
