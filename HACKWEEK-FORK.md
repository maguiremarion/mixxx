# Hackweek fork of Mixxx (branch `hackweek-dj-ui`)

This branch is Mixxx 2.7.0-alpha with changes for a touchscreen, two-deck DJ setup (a Pioneer
DDJ-FLX2 on a Raspberry Pi 4B, 1024x600 display). The skin ("RVD ix"), the controller mapping and
the Pi deployment scripts live in a separate repository, which also has the full feature table, the
Pi setup, the behaviour that differs from stock Mixxx, open items and lessons learned:

<https://github.com/maguiremarion/mixxx-hackweek-skin> (see its `README.md`)

Quick orientation for the C++ side:

- Touch UI: `src/widget/wnotice.*`, `wonscreenkeyboard.*`, `whotcuebutton.*`, `wlibrarysidebar.*`,
  `wtracktableview.cpp`, `wtracktableviewheader.*`, `weffectselector.*`
- Rekordbox / Serato / Computer start pages and the Rekordbox view: `src/library/rekordbox/`,
  `src/library/serato/`, `src/library/browse/`
- Startup, restart/quit, kiosk Preferences, dialog stylesheet: `src/mixxxmainwindow.*`,
  `src/preferences/dialog/dlgpreferences.cpp`, `src/coreservices.cpp`
- Sync master behaviour: `src/engine/sync/enginesync.cpp`
- Waveforms: `src/waveform/renderers/*/waveformrenderbeat.cpp` (beat ticks), `src/widget/wglwidgetqopengl.cpp`

Runs on Qt's **native Wayland** platform on the Pi (needs `qt6-wayland`): through XWayland the UI
stalled on round trips to the X layer. Build for the Pi with the scripts in the skin repo
(`pi/build-for-pi.sh`, `pi/push.sh app`).
