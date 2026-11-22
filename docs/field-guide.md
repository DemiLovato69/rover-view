# Field Rover field guide

[Back to the project overview](../README.md)

A touch-first Qt Quick survey collector for a Trimble 5700. The 5700 supplies dual-frequency GPS observations over serial. The application combines them with an NTRIP base in a native RTKLIB solver. The displayed fix is the **app's solution**, not the receiver's autonomous navigation record.

## Run on macOS

From the project directory:

```sh
brew install qt proj cmake ninja
cmake -S . -B build/mac -G Ninja
cmake --build build/mac --parallel
open build/mac/bin/fieldrover.app
```

The Mac development bundle uses the installed Qt and PROJ libraries. The first configure downloads the pinned RTKLIB sources and checksum-verified geoid grid. No Python interpreter or testing checkout is required by the application.

**Quick walkthrough, no equipment:**

1. Start in **Connect**. Tap **Start offline practice (90 s)** or continue to **Job** without a fix. Practice observations pass through the real solver.
2. In **Job**, resume a saved job or create `Practice` with `Synthetic ARP` and a **2.000 m** vertical pole height. Check antenna setup, reference frame, design and imagery before entering **Work View**.
3. In **Work View**, tap **Store point**. The ID advances on save. The position strip always shows latitude, longitude and the selected ground elevation reference, even with the grade gauge visible. Last-known readings show their age. An unavailable CGVD2013 elevation is not replaced with ellipsoid height.
4. Open **Points List** to inspect points and benchmark epochs or export CSV. Visit **Settings & Logs** for display settings, maps, receiver diagnostics and the current session's timestamped events. Changing screens does not stop collection or discard an occupation.

**Customize Work View:** Tap **Instruments** to show or hide the grade gauge, device bubble or 3D preview. Hiding the gauge does not change grading or what gets stored. With a loaded LandXML design, the small 3D view draws its actual visible triangles at true vertical scale and shows the rover at its ground elevation when compatible. Drag to orbit, pinch or tap to zoom, and reset the view. It opens in a separate nonmodal desktop window or a dismissible tablet panel. **Load design for preview only** preserves an existing manual grade target.

Use the appearance button in the header or **Settings & Logs** to select **Auto**, **Daylight**, or **Dark**. Auto follows system appearance. Display preferences survive restart. A sensor-equipped tablet can put a bubble beside the grade readout. In Settings, hold the tablet stationary and zero it with its rigidly attached pole independently plumb. Recalibrate after a mount change; calibration is stored for the sensor and orientation. A handheld tablet measures device tilt only. This instrument never applies a tilt correction to a surveyed point.

### Try cut/fill grading

The vertical gauge has a fixed ±250 mm scale, with 10 mm ticks and numbered 50 mm graduations. Above the zero line is **CUT** (orange); below it is **FILL** (blue). The green band marks the configured tolerance. Outside the scale the pointer stays at the end, while the readout gives the full difference in metres. If guidance becomes unavailable the pointer disappears. Point ID, code and **Store point** sit in a fixed capture panel in either orientation.

1. Create or open a practice job with a **2.000 m** vertical pole height. In **Job → Grade & surface setup** (also under **Work View → Instruments**), select **Design surface → Open practice surface**. Set tolerance to **0.020 m**, then tap **Use surface and references**.
2. In **Connect**, disconnect any previous session and tap **Start grading walk (36 s)**. The synthetic rover moves east at 1 m/s. Expect **CUT ~0.200 m** at the start, **ON GRADE** near 10 s, **FILL ~0.200 m** near 20 s, and **Outside design surface** after crossing the east edge. The same walk is available through a mock serial port with `python3 tools/mock5700.py --walk`.
3. To try a constant elevation, use the **stationary** practice replay instead. Choose **Grade… → Manual target**, enter **112.258 m**, and select **CGVD2013**. Ground should be about **112.058 m** and the instruction **FILL 0.200 m**. Positive ground-minus-design means cut; negative means fill. The numerical value is the full difference even when the visual bar reaches its end.

[practice-pad.landxml](../fixtures/practice-pad.landxml) is also a standalone file for **Open LandXML file…**. It defines a 40 × 40 m pad in EPSG:2959, rising 2% east. The importer reads named LandXML TIN surfaces in metres with a declared NAD83(CSRS) UTM EPSG code (zones 7–24N) and `verticalCoordinateSystemName` of `CGVD2013` or `Ellipsoid`. Choose a named surface and confirm the rover's frame matches before applying it. TIN vertices use **northing, easting, elevation** order. Only visible triangles supply elevations; edges are included, gaps aren't interpolated. Export explicitly clipped boundaries as clipped triangles first. The import limit is 16 MiB.

The active job stores its settings and a copy of the imported XML. The bar uses **ground height**, not antenna height. It clears the instruction when the epoch is stale, quality drops below the selected threshold (fixed by default), height is unavailable, or the rover leaves the TIN. Stored ordinary points and their CSV rows retain grade status, datum, tolerance, measured ground, target and signed difference when available, plus surface identity for a design job.

### Basemaps in the field

Choose **Local grid**, **Street**, **Satellite**, or **Offline** on the plot. Street uses OpenStreetMap at street scale. In Ontario, Satellite requests the provincial aerial imagery service through zoom 19 (actual resolution and coverage vary); elsewhere the default is the historical NASA regional overview through zoom 12. **Settings & Logs → Online satellite imagery** accepts a licensed HTTPS XYZ template, attribution and maximum native zoom. An endpoint with query parameters is kept for the session only. The map reports native zoom limits rather than claiming extra image detail. Tiles load asynchronously; zooming preserves the location under the zoom gesture. Online maps and RTK corrections have independent status.

For a network-free walkthrough, open a practice job, tap **Settings & Logs → Use practice-pad orthophoto (offline)**, then choose **Offline** in Work View. The bundled [raster MBTiles](../fixtures/practice-aerial.mbtiles) covers the practice pad at zooms 17–19 with DRAPE 2024 aerial photography (see [NOTICE.md](../NOTICE.md)). For another site import a licensed EPSG:3857 raster MBTiles package (JPEG, PNG, or supported WebP) with your coverage. It is copied and checked in the background before replacing the job's previous map. TMS row numbering is the default; explicitly declared XYZ is supported. Check coverage, acquisition date if supplied, size and native zoom before heading out. A missing tile leaves a grid with survey overlays and working capture controls.

**Known-point check:** Reoccupy a saved fixed point or fixed benchmark. In **Settings & Logs**, select it and run the check. A later fresh fixed epoch in the same known frame and from the same base is required. Horizontal geodesic distance and signed ground-height difference (current minus stored) are reported without changing the point, base coordinates or calibration. This is a repeat check, not independent proof of absolute accuracy.

### Delete a stored record

Open **Points List**, tap the record, and choose **Delete point…** or **Delete benchmark…**. Check the ID and job name, then tap **Delete**. A benchmark's accepted epochs are deleted with it. Deletion is permanent, updates the plot and future exports, and does not change previously exported CSV files.

### Test the serial connection and NTRIP client

In another terminal, run:

```sh
python3 tools/mock5700.py
```

The first two lines are a pseudo-terminal path (e.g. `/dev/ttys009`) and a localhost caster URL. In **Connect**, enter the printed serial path, select **Local simulator**, and tap **Connect receiver and base**. If using `--port 0` or another port, paste the printed caster URL. Disconnect any active practice session first. No credentials are needed. Use a separate job for simulated data.

The simulator answers the 9600-baud identity probe, link acknowledgement, satellite/ephemeris requests and multi-page enhanced RT17 observation requests. Its time-matched synthetic base streams valid RTCM3 station coordinates and dual-frequency observations (1005/1004); the field caster uses MSM4. The pseudo-terminal exercises framing and request/response timing, not electrical baud switching. Synthetic results are labeled in the UI, saved records, and CSV.

| Command using the printed simulator PID | Expected result |
|---|---|
| `kill -USR1 PID` | Toggle base transmission. Corrections become stale, then the solver falls back to standalone while rover epochs continue. A benchmark stops counting qualifying time. |
| `kill -USR2 PID` | Toggle repeated rover epochs. The live marker becomes **Last known** and **Store point** is disabled. |
| `kill -HUP PID` | Corrupt the next observation reply. The incomplete epoch is rejected. |
| Ctrl-C in the simulator terminal | Disconnect; existing points remain in the job. |

## Connect a receiver

Use the receiver's serial data collector cable and an appropriate USB-to-RS-232 adapter. Close other serial readers. Select the device in **Connect** (or enter its desktop port path). The app probes at 9600 8N1, validates a 5700 identity, switches to the reported data rate and polls observations without reconfiguring the receiver. Keep the device clock current for the firmware's GPS week rollover correction.

Select **RTK2go · OsgoodeFarms**, enter the rover email and use the tested password `none`; the mountpoint is case-sensitive. The tested endpoint uses unencrypted HTTP. The profile URL is editable for another compatible NTRIP v2 base transmitting GPS observations and station coordinates. Credentials are used in memory, not saved to the job. Receiver status, base-observation status, and fix quality are independent. Corrections are consumed by the app, not sent to the receiver.

On Android, select a supported USB host adapter, grant the system's USB permission, then tap **Connect** again. The USB bridge uses usb-serial-for-android's default driver probe. The physical adapter, tablet power arrangement and detachment/reconnection behavior still need testing on the selected hardware.

## Survey records

- **Point:** one fresh, distinct GPS epoch, with original quality (including standalone/float), coordinates, satellites, available correction age/ratio, source and base information. The local grid plot is for navigation, not a coordinate projection for export.
- **Benchmark:** qualifying duration and individual epochs, averaged in Earth-centered Cartesian coordinates. Horizontal and vertical scatter are population RMS about that mean in the local east/north/up axes. Fixed-only is the default; float can be explicitly included. Gaps and excluded epochs do not advance time. An interrupted receiver session or changed base cannot silently finish the occupation. If the pole moves, discard and restart.
- **Heights:** antenna ellipsoid, ground ellipsoid, and (inside grid coverage) antenna/ground CGVD2013 are separate metre fields. Ground heights subtract the measured vertical ARP offset once. The CGG2013a conversion assumes NAD83(CSRS)v6; record the actual base frame/epoch, or leave it unknown. An antenna model name documents setup; it does not apply a phase-center calibration. Neither ambiguity ratio nor occupation scatter establishes absolute accuracy.
- **Persistence:** each capture is an atomic SQLite insert, including the benchmark's contributing epochs. A setup edit affects future captures only. On macOS the database is `~/Library/Application Support/FieldRover/FieldRover/field.sqlite`; other targets use Qt's application-data directory. Unsaved occupations are not resumed after process exit. CSV exports summaries and provenance; individual accepted benchmark epochs remain in the database.

## Build targets

Use CMake 3.24+, a C/C++17 toolchain, Qt 6.5+ with Quick, Quick Controls, Network, Concurrent, Sensors, SQL/SQLite and (desktop) Serial Port, and PROJ built with TIFF support. Desktop tests additionally use Qt Test. Build commands and target toolchains are in [the build workflow](../.github/workflows/build.yml).

| Target | Build |
|---|---|
| Linux | With the development packages and QML runtime modules installed, `cmake -S . -B build/linux -G Ninja && cmake --build build/linux`. Launch `build/linux/bin/fieldrover`. |
| Windows x64 | Use an MSVC x64 Qt kit and vcpkg `proj[core,tiff]:x64-windows`. Configure with the vcpkg toolchain file and Qt's prefix, then `cmake --build build --config Release`. |
| Android arm64 | Use a matching Qt Android and host kit, Android SDK/NDK, Java 17 and target-built static PROJ/TIFF. Configure using vcpkg's toolchain with the Qt Android toolchain chained beneath it (so TIFF's compression libraries are linked), then build `--target apk`. The workflow gives the exact commands. Min SDK 28. |
| macOS | Development and mock-serial walkthrough above. |

The CI workflow builds desktop targets, runs tests, stages desktop installs, and builds an Android APK. Run it on your repository before distributing a target package.

## Verification

```sh
ctest --test-dir build/mac --output-on-failure
```

Desktop tests load the touch UI and verify geoid conversion, averaging, reopening the database and deterministic replay. The 90 synthetic epochs in `fixtures/practice.bin` are compared with `fixtures/reference.csv`, obtained from the testing repo's original C engine with RTKLIB time set to the fixture's GPS week 2400, TOW 200000. Regenerate the binary with `python3 tools/mock5700.py --write-replay fixtures/practice.bin`.

The grading suite has passed on macOS arm64 (Qt 6.12) and Fedora Linux arm64 (Qt 6.11), including point capture with unreachable imagery tiles and replay across the practice surface. The 90-epoch reference check also covers the b34 first-iteration atmospheric correction initialization in the CMake build. Windows x64 is cross-compiled with MinGW; the Android arm64 build uses Qt 6.8.3. Cross-builds do not exercise the target OS's device drivers.

On macOS/Linux, tests also connect through a real pseudo-terminal and local HTTP caster, tap the QML capture button with a touch event, rotate during an occupation, check duplicate and stale capture rejection, drop corrections, and export the stored job. The original hardware bench run demonstrated float, not sustained fixed RTK. Validate repeat occupations on independent control before relying on field accuracy.

See [NOTICE.md](../NOTICE.md) for source and grid attribution.
