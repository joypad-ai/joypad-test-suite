MIT License

Copyright (c) 2026 Robert Dale Smith

The Joypad Tester 3DO ROM source (`src/main.cpp`) is original work
licensed under MIT.

The build pipeline depends on Antonio SJ Musumeci's
[3do-devkit](https://github.com/trapexit/3do-devkit) (ISC), which is
cloned at a pinned commit by `buildtools/Dockerfile` and supplies:

- the Norcroft ARM C/C++ compilers,
- 3DO Portfolio SDK headers / libraries,
- the `BasicDisplay` C++ helper class + `abort_err` panic handler
  (used unmodified from the devkit's `src/`).

`BasicDisplay` (`display.cpp`/`.hpp`) and `abort.c`/`abort.h` are
trapexit's ISC code, linked into our LaunchMe as part of the
standard devkit build. Their copyright + ISC notice live in the
devkit repository.

The 3DO Portfolio SDK headers (`include/3dosdk/`) are originally
copyrighted by The 3DO Company; they are widely redistributed
within the 3DO homebrew scene and ship inside the trapexit devkit
that we depend on at build time.

---

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
