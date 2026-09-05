# Official Moxa downloads

Downloaded 2026-09-03 from the official Moxa UC-7402 Plus product resource.
This directory and its audit extraction are intentionally ignored by Git.

| Artifact | Version | Source | Bytes | Published/local SHA-512 |
|---|---:|---|---:|---|
| `moxa-uc-7402-plus-series-tool-chain-for-uc-74xx-lx-plus-and-da-66x-16-lx-series-software-package-v1.2.sh` | 1.2 | `https://cdn-cms-frontdoor-dfc8ebanh6bkb3hs.a02.azurefd.net/Moxa/media/PDIM/S100000450/moxa-uc-7402-plus-series-tool-chain-for-uc-74xx-lx-plus-and-da-66x-16-lx-series-software-package-v1.2.sh` | 166332646 | `7E453C9CB1311C87B829485B770CD8AF9BB96961B90E5A492BCD61A3685E3164DE5BD2E7254976112059463F468C8168D737F6C051366DF181C4EAD8C9DB2645` |
| `moxa-uc-7402-plus-series-example-for-uc-74xx-lx-plus-series-da-66x-16-lx-series-library-v1.4.zip` | 1.4 | `https://cdn-cms-frontdoor-dfc8ebanh6bkb3hs.a02.azurefd.net/Moxa/media/PDIM/S100000450/moxa-uc-7402-plus-series-example-for-uc-74xx-lx-plus-series-da-66x-16-lx-series-library-v1.4.zip` | 2526507 | `07D63830AA0907F18C639DB2E5EC85A55EEB158B88D3EF26461BE1CC9FDB9C01620C643A3641FF4BB309CCCB69E7E68B9589B11077C65F2826E8501D5455A6AE` |

Both local hashes matched the published hashes exactly before extraction.
The shell installer was not executed. Its gzip/tar payload starts after line
72 (`__ARCHIVE_FOLLOWS__`) and was unpacked under `_audit/toolchain`. The ZIP
was listed before being unpacked under `_audit/examples`.
