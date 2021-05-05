Artifacts
=

Listing
==

You can always check out the available `ddprof` and `collatz` binaries with something like:
```
aws-vault exec build-stable-developer -- aws s3 ls s3://binaries.ddbuild.io/ddprof/release/
```


Base Image
===

**where**: 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:base

**how**: app/base-env

**when**: manual job in CI

This image contains the system-level requirements for building `ddprof`.


Build Image
===

**where**: 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:build

**how**: app/base-env

**when**: manual job in CI

This image layers the base, adding intermediate build artifacts from dependencies.  Since `ddprof` has a one-shot build system, the artifacts represent code and build resources rather than binary objects.  Dependencies are currently only `elfutils` and `libddprof`.


ddprof
===

**where**: binaries.ddbuild.io/ddprof/release/ddprof

**how**: `make build`

**when*: automatic job in CI, executing when `branch == main`

This is the native profiler.  It is shipped as a binary.


collatz
===

**where**: binaries.ddbuild.io/ddprof/release/collatz

**how**: `make bench`

**when**: manual job in CI

This is a benchmarking tool for the native profiler.


Versioning
==

`ddprof` and `collatz` are versioned in binaries.ddbuild.io with something like the following scheme.

* `ddprof/release/ddprof`: bleeding edge release
* `ddprof/release/ddprof_X.Y.X`: last passing build for a given patch version
* `ddprof/release/ddprof_X.Y.Z_CIID-SHORTHASH`: pinned build, where CIID used to be the IID for CI, but is now the ID for quick reference.
* `ddprof/release/ddprof_X.Y.Z_rcN`: release candidate (not yet implemented, may never be used)
* `ddprof/release/ddprof_X.Y.Z_final`: release build (not yet implemented)

`ddprof` and `collatz` will always report their own most verbose version information.  Release builds are generated manually (actually, not at all right now).  Information is injected during the build process, so the executables can always report `tool --version` correctly.  When uploaded back to S3, `tool --version` is used rather than CI/git metadata; hopefully the slight inefficiency persists a strong binding between the various forms of metadata.