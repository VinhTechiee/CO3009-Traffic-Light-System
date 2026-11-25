# DHT20 protocol layer

This layer parses a completed measurement only. It has no transport, clock,
mutable module state, allocation, or initialization dependency. The protocol
phase originally left `dht20.h` and `dht20.c` unchanged; the subsequent driver
phase implements them as described in [dht20-driver.md](dht20-driver.md).

## Verified manufacturer references

Reviewed on 2026-11-16:

- Aosong/ASAIR, **Data Sheet DHT20**, V1.0, May 2021,
  [manufacturer-hosted English PDF](https://asairsensors.com/wp-content/uploads/2021/09/Data-Sheet-DHT20-Humidity-and-Temperature-Module-ASAIR-V1.0.03.pdf).
  Table 9 and sections 7.3-7.4 (page 10) describe status and CRC;
  the read diagram and sections 8.1-8.2 (page 11) describe packing and units.
- Aosong, **DHT20 product specification**, V1.3, September 2024,
  [manufacturer document reproduced by Manuals+](https://manuals.plus/m/6fb94a2f5bcc906a21ab17092318ce41f04d0b1bcdb237cbcf11e6dbf6c72dbb).
  Table 8 and section 5.2 explicitly define bit 4 as OTP integrity status
  and include the manufacturer's MSB-first CRC reference algorithm.
  The hosting page title says A1, but the document itself identifies V1.3.

V1.0 labels bit 4 reserved in its status table, but requires the startup
status mask `0x18`. V1.3 explains bit 4. This parser requires bit 4 and the
calibration-enable bit 3 for every accepted frame as a conservative publication
policy. The future driver must still perform the appropriate startup checks.

V1.3 table 8 defines the complete status byte as follows:

| Bits | Meaning | Parser policy after frame CRC validation |
| --- | --- | --- |
| 7 | Busy status | Must be clear; otherwise `APP_BUSY` |
| 6-5 | Operating mode | Intentionally ignored for measurement conversion |
| 4 | OTP integrity | Must be set; otherwise `APP_IO_ERROR` |
| 3 | Calibration status/enable | Must be set; otherwise `APP_NOT_READY` |
| 2 | CMP indication | Intentionally ignored for measurement conversion |
| 1-0 | Reserved | Intentionally ignored; no assumed values |

Operating mode and CMP indication are defined bits, not reserved bits. They do
not affect this parser's conversion policy. Ignored bits still participate in
frame CRC calculation. Busy, OTP integrity, and calibration are checked in that
order after CRC validation.

## Algorithm and contract

Input is exactly seven bytes: status, five packed measurement bytes, CRC.
CRC covers bytes 0 through 5, including status. Width is 8, polynomial is
`x^8 + x^5 + x^4 + 1` (`0x31`), initial remainder `0xFF`. XOR each byte into
the remainder, then shift left eight times, XORing `0x31` when the outgoing
top bit is set. Truncate to eight bits each step. There is no input/output
reflection or final XOR, matching the manufacturer's reference code.

After CRC validation, bit 7 rejects busy measurements. Bit 4 must indicate
OTP integrity and bit 3 must indicate calibrated output.

For frame bytes `b0..b6`:

- Humidity raw: `(b1 << 12) | (b2 << 4) | (b3 >> 4)`.
- Temperature raw: `((b3 & 0x0F) << 16) | (b4 << 8) | b5`.
- Humidity: `raw * 100 / 2^20`, in percent RH.
- Temperature: `raw * 200 / 2^20 - 50`, in degrees Celsius.

The parser preserves all output fields on failure and sets `valid=true` only
on success. Callers must check the return status; retained output is not a new
measurement. Error priority is invalid argument/length, CRC, busy, OTP integrity,
then calibration. CRC/OTP errors use `APP_IO_ERROR`, busy uses `APP_BUSY`, and
disabled calibration uses `APP_NOT_READY`; shared status types are unchanged.

The encoding endpoints are 0 to just below 100 percent RH and -50 to just below
150 degrees Celsius. These are encoding limits, not rated operating limits.
No application range rejection, freshness policy, cadence, or timing is added.

## Host tests

From the repository root, using GCC in PowerShell:

```powershell
gcc -std=c99 -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow -ICore/Inc Core/Src/dht20_protocol.c tests/test_dht20_protocol.c -o "$env:TEMP/co3009_dht20_protocol_tests.exe"
& "$env:TEMP/co3009_dht20_protocol_tests.exe"
git diff --check
```

Tests use assertions (do not define `NDEBUG`). A separate polynomial long-division
oracle checks CRC, including a fixed expected vector. Frames are synthesized,
not recordings from hardware. Tests cover corruption of every frame bit,
all status bytes, length/null rejection, packing across the shared nibble,
and 169 conversion endpoint/midpoint pairs. Every failure tested with a writable
sample output is repeated with both `valid=false` and `valid=true`, nonzero
temperature/humidity, field comparisons, and a byte-for-byte snapshot comparison.
This includes CRC errors, busy, disabled calibration, failed OTP integrity, null
frame input, and invalid lengths. A null output pointer is tested separately.

Forty additional walking-bit frames exercise each of the 20 humidity bits and
each of the 20 temperature bits in isolation. A hand-specified table maps wire
byte/mask locations to explicit decimal raw weights, without using `make_frame`
or reproducing the parser's shift expressions. Each case also verifies that the
other measurement remains at its zero-raw baseline. These are synthetic project
fixtures, not manufacturer test vectors. CRC bytes come from the independent
polynomial long-division oracle.

## Firmware integration

The cooperative driver now acquires seven coherent bytes through an injected
asynchronous transport, handles startup/measurement/recovery, and publishes
protocol-valid frames with their observed reception timestamp. Delayed valid
frames publish as stale; invalid frames preserve last-good data. Reconfiguration
preserves sample age. See [driver policy and integration](dht20-driver.md).
The firmware uses the STM32 I2C1 backend for sensor transactions. Physical bus
configuration and sensor response remain part of board validation.
