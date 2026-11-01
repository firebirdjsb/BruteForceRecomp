# Public runtime MMIO decoder

`mmio_decode.h` is copied unchanged from [xenophobentx/xboxrecomp](https://github.com/xenophobentx/xboxrecomp/blob/ebcbf6ff086bea64b5181741951f0d815db90968/src/platform/mmio_decode.h), commit `ebcbf6ff086bea64b5181741951f0d815db90968`, under the accompanying MIT license. It extends the common x64 decoder to Linux `ucontext_t` while retaining Windows support. This file contains generic host instruction decoding, no game code or title-specific addresses.

Only private build copies of three pinned runtime source files are adapted to this shared entry point. Unknown instructions remain unhandled faults. The original ARM64 decoder remains supplied by the pinned baseline toolkit.
