# snes9x-gilgamesh <a href='https://www.recurse.com' title='Made with love at the Recurse Center'><img src='https://cloud.githubusercontent.com/assets/2883345/11325206/336ea5f4-9150-11e5-9e90-d86ad31993d8.png' height='20px'/></a>

This is a fork of Snes9x (a SNES emulator) with advanced tracing capabilities. It logs the entire execution flow (including memory accesses) on a SQLite database for subsequent analysis.

# Usage
Compile the emulator:
```
cd unix
./configure
make
```

Run your favorite game:
```
./snes9x awesomegame.sfc
```

Once you're finished playing, press `Ctrl+C` on the console, then run the command `q` in the debugger shell. That will ensure the data is saved on the database (`~/.snes9x/log/gilgamesh.db`).
