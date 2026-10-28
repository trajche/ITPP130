# ITPP130 Linux driver

Open source CUPS driver for the **MUNBYN ITPP130** 4-inch thermal label printer.

MUNBYN ships drivers for Windows and macOS only. This project adds Linux
support, including Raspberry Pi, so the printer can be used with CUPS,
PrintNode, or any other CUPS-based print server.

It contains:

- `rastertotspl`, a CUPS filter that converts CUPS raster into TSPL, the
  printer's native command language.
- A PPD with the printer's label sizes and settings: media tracking
  (gap / black mark / continuous), gap height and offset, darkness, speed,
  horizontal and vertical offset, print direction, and a dotted line between
  labels.

## Install

### From a release (Debian, Ubuntu, Raspberry Pi OS)

Download the `.deb` for your architecture (`arm64` for a 64-bit Raspberry Pi,
`amd64` for a PC) from [Releases](https://github.com/trajche/ITPP130/releases),
then:

```sh
sudo apt install ./munbyn-itpp130-cups_*.deb
```

### From source

```sh
sudo apt install build-essential libcups2-dev libcupsimage2-dev cups-ppdc
make
sudo make install
```

This installs the filter to `/usr/lib/cups/filter/rastertotspl` and the PPD to
`/usr/share/ppd/munbyn/munbyn-itpp130.ppd`.

## Add the printer

Plug the printer in over USB and find its device URI:

```sh
lpinfo -v | grep ITPP130
```

Then create the queue, using the URI from the previous command:

```sh
sudo lpadmin -p MUNBYN_ITPP130 -E \
  -v 'usb://Printer/ITPP130?serial=XXXXXXXXXXXX&interface=1' \
  -P /usr/share/ppd/munbyn/munbyn-itpp130.ppd
```

You can also add it from the CUPS web interface at `http://localhost:631` by
choosing **MUNBYN ITPP130** as the model.

## Options

Set defaults with `lpadmin -p MUNBYN_ITPP130 -o NAME=VALUE`, or per job with
`lp -o NAME=VALUE`.

| Option | Values | Default |
|---|---|---|
| `PageSize` | `w288h432` (4×6 in) and 45 other label sizes, see `lpoptions -p MUNBYN_ITPP130 -l` | `w288h432` |
| `zeMediaTracking` | `Gap`, `BLine`, `Continuous` | `Gap` |
| `GapOrMarkHeight` | `0` to `10` (mm) | `3` |
| `GapOrMarkOffset` | `0` to `10` (mm) | `0` |
| `Darkness` | `Default`, `0` to `15` | `8` |
| `zePrintRate` | `Default`, `2` to `8` (in/sec) | `4` |
| `AdjustHorizontal` | `-20` to `20` (mm) | `0` |
| `AdjustVertical` | `-20` to `20` (mm) | `0` |
| `Rotate` | `0` (normal), `1` (180°) | `0` |
| `AutoDotted` | `0`, `1` | `0` |

If labels print too light, raise `Darkness`. If they drift between labels,
check that `zeMediaTracking` and `GapOrMarkHeight` match your label stock.

## How it works

For each page the filter sends:

```
<1024 NUL bytes>
SIZE <w> mm,<h> mm
REFERENCE <x>,<y>
DIRECTION <n>,0
GAP <h> mm,<offset> mm        (or BLINE ..., or GAP 0 mm,0 mm for continuous)
DENSITY <n>
SPEED <n>
SETC AUTODOTTED ON|OFF
SETC PAUSEKEY ON
SETC WATERMARK OFF
CLS
BITMAP 0,0,<width bytes>,<height>,1,<raw 1-bit rows>
PRINT 1,1
```

CUPS renders each page as 8-bit grayscale at 203 dpi. The filter thresholds
it to 1 bit per pixel (values below 201 print black) and packs 8 pixels per
byte, with a 1 bit meaning white, as TSPL expects. The leading NUL bytes clear
any partial command left in the printer's parser.

The command stream matches what MUNBYN's macOS driver sends. It was worked out
for interoperability by studying that driver's behavior. This repository does
not contain any MUNBYN code or files.

## Testing

`make test` renders the CUPS test page to raster with the PPD, runs the filter,
and checks the TSPL header, bitmap size and trailer. It needs `cups`,
`cups-filters` and `ghostscript`.

## License

[Apache License 2.0](LICENSE).

MUNBYN is a trademark of its owner. This project is not affiliated with or
endorsed by MUNBYN.
