# Compiling C/C++ code

The present document compiles a list of guidelines, recommendations, and
expectations around the topic of compiling C and C++ code against the Core SDK.


## Sysroot

The Fuchsia sysroot for a given target architecture is available under
`//arch/<architecture>/sysroot`.
That directory contains a complete sysroot and may be used with any tool that
accepts a `--sysroot` flag.


## Prebuilts

All prebuilts have C linkage.

### Debug symbols

Debug symbols for all prebuilts are available under `//.build-id`, which follows
a [standard convention][build-id].


## Compilation parameters

- C++ sources are compatible with both C++14 and C++17.

### Warning flags

The following flags are guaranteed to not generate any warning:
- `-Wsemi-extra`

The following flags may generate warnings:
- `-Wdeprecated-declarations`


[build-id]: https://fedoraproject.org/wiki/Releases/FeatureBuildId#Find_files_by_build_ID
