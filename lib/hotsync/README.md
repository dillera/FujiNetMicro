# HotSync server

FujiNet acts as the desktop side of a Palm OS HotSync. A device that syncs with
it gets the apps and databases queued on the SD card, its Date Book gets the
events of a Google or iCalendar calendar, and its databases are backed up to
the SD card. It is a service, not a bus: it runs on its own thread
beside whatever platform the firmware was built for.

A C++ port of the protocol stack in [palm-sync](https://github.com/jichu4n/palm-sync).

## Using it

Add to `fnconfig.ini` (an SD card is required):

```ini
[HotSync]
enabled=1
user=FujiNet          ; given to a device that has never synced
backup=flagged        ; none | flagged (backup bit set, like Palm Desktop) | all
netsync_port=14238    ; network HotSync; 0 turns it off
emulator_port=6416    ; serial-over-TCP for POSE / CloudpilotEmu; 0 turns it off
serial_port=          ; a cradle: "bus" on the ESP32, a device path on FujiNet-PC
calendar=GCAL:///     ; copied into the Date Book; empty leaves it alone
calendar_days_back=7  ; how much of the calendar to copy
calendar_days_ahead=60
```

The same settings are in the web UI's **Palm HotSync** panel; they take effect
when FujiNet restarts.

`serial_port=bus` puts the cradle on the RS232 bus port, shared with the bus.
The `fujinet-rs232-s3-palm` board builds the FN-RS232 this way by default;
connect the cradle to its DB-9 through a null modem.

The SD card layout, under `/palm`:

| Folder              | Contents                                               |
|---------------------|--------------------------------------------------------|
| `install/`          | `.prc`, `.pdb`, `.pqa` files to install on next sync   |
| `installed/`        | files moved here after a successful install           |
| `backup/<user>/`    | databases read back from the device                    |
| `state/<user>/`     | `datebook.map`: the Date Book records the calendar made |

## The calendar

`calendar` is a devicespec for FujiNet's own calendar protocols, without a
view: `GCAL:///` for the calendars shown in Google Calendar, `GCAL://Work/` for
one of them, or `ICAL://host/feed.ics` for an iCalendar feed. GCAL uses the
Google grant made under **Google Account** in the web UI. Every ten minutes the
service fetches the window it covers, so a sync does not keep the Palm waiting.

Each sync copies those events into DatebookDB, one way:

- an event becomes an appointment; an all-day event an untimed one, repeating
  daily when it spans days; one that runs past midnight stops at 23:59;
- the location goes in the appointment's note;
- an event that changes is rewritten, and one that is cancelled is deleted;
- appointments made on the Palm are never touched, and neither is a copied one
  that was edited on the Palm and then cancelled in the calendar.

Times are shown in `[General] timezone`; with none set, the service reads the
Palm's offset from UTC off its clock.

## The cradle on the bus

A HotSync always opens at 9600 baud, and a 9600-baud byte read at the bus rate
is a line break the UART drops, so the two cannot share one rate. The service
borrows the bus port for 1.5 s at 9600 to catch a WAKEUP, then gives it back to
the bus for 1.5 s (`HotSyncBusPort.h`). In between, a Palm app talks to FujiNet
with FujiBus, like any RS232 host. The device repeats its WAKEUP for longer
than one cycle. A Palm running an app cannot HotSync, so for a minute after the last FujiBus
packet the service leaves the port to the bus, unless bytes that are not
FujiBus arrive with no packets, which is what a HotSync start looks like.

## Layout

| File                    | Layer                                                    |
|-------------------------|----------------------------------------------------------|
| `HotSyncLink.h`         | raw byte link (UART, socket, test fixture)              |
| `Slp.*`                 | Serial Link Protocol framing and CRC                    |
| `PadpTransport.*`       | PADP fragments, ACK/retry, and the CMP handshake        |
| `NetSyncTransport.*`    | NetSync framing and handshake                           |
| `Dlp.*`, `DlpClient.*`  | Desktop Link Protocol encoding and typed commands       |
| `PalmDatabase.*`        | `.pdb`/`.prc` files                                     |
| `HotSyncSession.*`      | one sync: identify, install, Date Book, back up, stamp user info |
| `Datebook.*`, `DatebookConduit.*` | Date Book records, and copying events into them |
| `HotSyncCalendar.h`, `HotSyncNetCalendar.*` | events, read through GCAL:/ICAL:   |
| `HotSyncStorage.h`, `HotSyncFsStorage.*` | files on the SD card                   |
| `HotSyncBusPort.h`      | the bus port a cradle borrows                           |
| `HotSyncLinks.*`, `HotSyncService.*`     | FujiNet sockets, serial, and thread    |

Everything up to `HotSyncSession` depends only on `include/global_types.h` and
`fn_time`, so `tests/HotSyncTests.cpp` drives it with frames captured from a
real device and a fake one.

`tools/hotsync-fakepalm/fake_palm.py` is a stand-in Palm that syncs over
network HotSync, for testing a FujiNet or FujiNet-PC end to end without a
device in a cradle.

## Not done yet

- Two-way record sync. The Date Book gets the calendar's events, but
  appointments made on the Palm do not go back to the calendar; Memo and
  Address are only backed up.
- The web UI does not show the last sync's result.
- Installs and backups hold a whole database in RAM, which is fine on boards
  with PSRAM but tight without it.
