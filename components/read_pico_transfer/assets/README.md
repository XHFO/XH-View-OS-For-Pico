# Precompressed device pages

`../manage.html` and `../upload.html` are the only editable HTML sources. Their
gzip archives are checked-in generated build resources. Normal firmware builds
need only Python's standard library: `compress_management.py` decompresses each
archive and compares every byte with the HTML before copying it to the build.
Missing, corrupt, or stale archives fail the build; there is no fallback.

To regenerate after editing a page, install the optional developer dependency
`zopfli==0.4.0` in an isolated environment and run from the repository root:

```sh
python3 components/read_pico_transfer/compress_management.py \
  components/read_pico_transfer/manage.html /tmp/manage.html.gz --regenerate
python3 components/read_pico_transfer/compress_management.py \
  components/read_pico_transfer/upload.html /tmp/upload.html.gz --regenerate
```

Generation uses `zopfli.gzip.compress(raw, numiterations=30)`, with gzip mtime zero
and no filename. Commit both the changed HTML source and its generated archive.
The production handlers serve these same bytes with `Content-Encoding: gzip`.
