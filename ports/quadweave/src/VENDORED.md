# Source provenance

No third-party engine or parser source is vendored. MIDI, progression, playback, browser and wrapper glue were implemented in this repository. Shared VST ABI declarations were extracted unchanged from `wrapper/vst2_wrap.c` into `wrapper/vst2_abi.h`; the existing DSP wrapper includes that header too.

The `.progression` schema was inspected in the author-maintained source repository `https://github.com/liotier/AkaiMPC` at commit `0593d6dcf7139157a67360c6ce4388df7d7cb1e6`, specifically `AkaiMPCChordProgressionGenerator/docs/mpc-probe/ZZProbe_2_flat.progression`. Only the field structure was used; no chord pack or source was copied. Test progressions are small synthetic fixtures.

ALSA is linked as a system dependency. An x86 compile check used temporary headers from alsa-lib v1.2.8, commit `9447e57d7c1602a861635487ca56c452f3472965`, with the environment's `libasound.so.2`; these headers are not distributed here. Normal builds use the platform's libasound2-dev package.

Do not distribute user MIDI collections or proprietary progression packs. Repository/third-party distribution licensing must be resolved before publishing a release; this implementation does not invent a license for the shared repository code.
