# OFSExtractor (based on MVCPlanes2OFS from BD3D2MK3D's toolset)

This program can extract depth values, aka 3D-Planes, from MVC streams found on 3D Blu-rays.

The 3D-Planes will then be placed inside OFS files which can be used with Blu-ray authoring software,
or can also be used with programs such as BD3D2MK3D to convert subtitles to 3D.

## Usage

```
$ OFSExtractor [-license] <input file> <output folder> [-fps # -dropframe]
```

| Option | Description |
| ------ | ----------- |
| `-license` | Prints the license. |
| `<input file>` | Can be a raw MVC stream, a H264+MVC combined stream (like those from MakeMKV) or a M2TS file. (M2TS is not fully supported, and could burn your toast.) |
| `<output folder>` | The output folder which will contain the OFS files. If undefined the current directory will be used. |

### Advanced Options: Use with care!

| Option | Description |
| ------ | ----------- |
| `-fps` | Must be a value between 1 and 4, 6, or 7. See Table. This will override the fps value that would normally come from the MVC stream. |
| `-dropframe` | Set drop\_frame\_flag within the resulting OFS files. Can only be used with FPS value 4. |

### FPS Conversion Table:

| Value | FPS |
| ----- | --- |
| 1 | 23.976 |
| 2 | 24 |
| 3 | 25 |
| 4 | 29.97 |
| 6 | 50 |
| 7 | 59.94 |

## Compiling Steps

The only requirements I can think of is meson, and mingw64 (if compiling for Windows).

If building on windows I recommend using msys2, or WSL.

### For Linux

```
$ meson --buildtype release build
$ cd build
$ ninja
```

You should now have a binary called `OFSExtractor`.\
It can be installed via `ninja install`.

### Cross compiling for Windows (via MingW64)

```
$ meson --cross-file build-win32(or 64).txt --buildtype release
$ cd build
$ ninja
```

You should now have an exe file called `OFSExtractor32(or 64).exe`.

## Acknowledgments

Thank you Nico8583 on doom9 for letting me see the source code to MVCPlanes2OFS,\
and thank you r0lZ on doom9 for making BD3D2MK3D, and for helping me understand how to actually make this program.
