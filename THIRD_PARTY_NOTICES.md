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

The Windows build pins that archive by SHA-256 and applies the semantic
compatibility changes from Debian's long-maintained ZThread patches 020, 050,
and 070 for modern GCC. Those reproducible transformations are recorded in
`scripts/build-windows-client.sh`.

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

## Space Grotesk

The interface font. The client draws text from bitmap atlases in
`textures/ui/`, rasterised by `scripts/generate-rcl-ui-font.py` from the font
file the RCL site serves. The title card, the window icon and the Windows
icon (`textures/title.png`, `textures/icon.png`, `tron.ico`) are set in the
same family by `scripts/generate-rcl-brand.py`, from the upstream variable
font at the revision recorded next to it. Both font files live in
`scripts/assets/fonts/space-grotesk/` and are not part of binary packages.

> Copyright 2020 The Space Grotesk Project Authors (https://github.com/floriankarsten/space-grotesk)
>
> This Font Software is licensed under the SIL Open Font License, Version 1.1.
> This license is copied below, and is also available with a FAQ at:
> http://scripts.sil.org/OFL
>
>
> -----------------------------------------------------------
> SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
> -----------------------------------------------------------
>
> PREAMBLE
> The goals of the Open Font License (OFL) are to stimulate worldwide
> development of collaborative font projects, to support the font creation
> efforts of academic and linguistic communities, and to provide a free and
> open framework in which fonts may be shared and improved in partnership
> with others.
>
> The OFL allows the licensed fonts to be used, studied, modified and
> redistributed freely as long as they are not sold by themselves. The
> fonts, including any derivative works, can be bundled, embedded,
> redistributed and/or sold with any software provided that any reserved
> names are not used by derivative works. The fonts and derivatives,
> however, cannot be released under any other type of license. The
> requirement for fonts to remain under this license does not apply
> to any document created using the fonts or their derivatives.
>
> DEFINITIONS
> "Font Software" refers to the set of files released by the Copyright
> Holder(s) under this license and clearly marked as such. This may
> include source files, build scripts and documentation.
>
> "Reserved Font Name" refers to any names specified as such after the
> copyright statement(s).
>
> "Original Version" refers to the collection of Font Software components as
> distributed by the Copyright Holder(s).
>
> "Modified Version" refers to any derivative made by adding to, deleting,
> or substituting -- in part or in whole -- any of the components of the
> Original Version, by changing formats or by porting the Font Software to a
> new environment.
>
> "Author" refers to any designer, engineer, programmer, technical
> writer or other person who contributed to the Font Software.
>
> PERMISSION & CONDITIONS
> Permission is hereby granted, free of charge, to any person obtaining
> a copy of the Font Software, to use, study, copy, merge, embed, modify,
> redistribute, and sell modified and unmodified copies of the Font
> Software, subject to the following conditions:
>
> 1) Neither the Font Software nor any of its individual components,
> in Original or Modified Versions, may be sold by itself.
>
> 2) Original or Modified Versions of the Font Software may be bundled,
> redistributed and/or sold with any software, provided that each copy
> contains the above copyright notice and this license. These can be
> included either as stand-alone text files, human-readable headers or
> in the appropriate machine-readable metadata fields within text or
> binary files as long as those fields can be easily viewed by the user.
>
> 3) No Modified Version of the Font Software may use the Reserved Font
> Name(s) unless explicit written permission is granted by the corresponding
> Copyright Holder. This restriction only applies to the primary font name as
> presented to the users.
>
> 4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
> Software shall not be used to promote, endorse or advertise any
> Modified Version, except to acknowledge the contribution(s) of the
> Copyright Holder(s) and the Author(s) or with their explicit written
> permission.
>
> 5) The Font Software, modified or unmodified, in part or in whole,
> must be distributed entirely under this license, and must not be
> distributed under any other license. The requirement for fonts to
> remain under this license does not apply to any document created
> using the Font Software.
>
> TERMINATION
> This license becomes null and void if any of the above conditions are
> not met.
>
> DISCLAIMER
> THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
> EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
> MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
> OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
> COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
> INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
> DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
> FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
> OTHER DEALINGS IN THE FONT SOFTWARE.
