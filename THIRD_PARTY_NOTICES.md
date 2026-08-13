# Third-party notices

Retrocycles RCL is distributed under the GNU General Public License version 2;
see `COPYING.txt`. Binary packages may also redistribute the libraries below.

## SDL2, sdl12-compat, and SDL_image

- SDL2: Copyright (C) 1997-2025 Sam Lantinga and SDL contributors.
- sdl12-compat: Copyright (C) 1997-2026 Sam Lantinga and SDL contributors.
- SDL_image 1.2: Copyright (C) 1997-2012 Sam Lantinga.

These components use the zlib license:

> This software is provided "as-is", without any express or implied warranty.
> In no event will the authors be held liable for any damages arising from the
> use of this software.
>
> Permission is granted to anyone to use this software for any purpose,
> including commercial applications, and to alter it and redistribute it
> freely, subject to the following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not claim
>    that you wrote the original software. If you use this software in a
>    product, an acknowledgment in the product documentation would be
>    appreciated but is not required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.

sdl12-compat includes code from
[dr_mp3](https://github.com/mackron/dr_libs), which may be treated as public
domain or used under MIT-0:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## libpng

PNG Reference Library License version 2:

Copyright (c) 1995-2026 The PNG Reference Library Authors.
Copyright (c) 2018-2026 Cosmin Truta.
Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.
Copyright (c) 1996-1997 Andreas Dilger.
Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

> The software is supplied "as is", without warranty of any kind, express or
> implied, including, without limitation, the warranties of merchantability,
> fitness for a particular purpose, title, and non-infringement. In no event
> shall the copyright owners, or anyone distributing the software, be liable
> for any damages or other liability, whether in contract, tort or otherwise,
> arising from, out of, or in connection with the software, or the use or other
> dealings in the software, even if advised of the possibility of such damage.
>
> Permission is hereby granted to use, copy, modify, and distribute this
> software, or portions hereof, for any purpose, without fee, subject to the
> following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not claim
>    that you wrote the original software. If you use this software in a
>    product, an acknowledgment in the product documentation would be
>    appreciated, but is not required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This copyright notice may not be removed or altered from any source or
>    altered source distribution.

## zlib

Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler.

zlib is distributed under the zlib license reproduced above.

## libxml2 2.14.5

The macOS client statically links libxml2 2.14.5, downloaded from the
[GNOME release archive](https://download.gnome.org/sources/libxml2/2.14/).

Except where otherwise noted in the source code, libxml2 carries this notice:

Copyright (C) 1998-2012 Daniel Veillard. All Rights Reserved.
Copyright (C) The Libxml2 Contributors.

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## ZThread 2.3.2

The Windows client statically links
[ZThread 2.3.2](https://sourceforge.net/projects/zthread/files/ZThread/2.3.2/),
Copyright (c) 2005 Eric Crahen. ZThread's exact upstream source archive
contains this MIT notice in `LICENSE` and `MIT.TXT`:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## Windows MinGW runtime libraries

Windows packages may include runtime DLLs supplied by MSYS2's MinGW-w64
toolchain and library packages. The packaging process identifies the owner of
every bundled DLL and includes that package's installed license files under
`ThirdPartyLicenses/MSYS2`.

- GCC runtime libraries (including libgcc and libstdc++) are distributed under
  [GPL version 3 or later](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html)
  with the
  [GCC Runtime Library Exception version 3.1](https://www.gnu.org/licenses/gcc-exception-3.1.html).
  Corresponding source is available from the
  [GCC project](https://gcc.gnu.org/git.html) and the matching MSYS2 source
  package.
- The MinGW-w64 runtime and winpthreads contain components under permissive
  licenses including the Zope Public License 2.1, BSD-style licenses, and
  public-domain dedications; some toolchain components may use the LGPL.
  Exact notices for the files shipped in a package are copied from the installed
  MSYS2 packages. Corresponding source and packaging metadata are available from
  the [MinGW-w64 project](https://www.mingw-w64.org/) and
  [MSYS2 package repositories](https://packages.msys2.org/).
