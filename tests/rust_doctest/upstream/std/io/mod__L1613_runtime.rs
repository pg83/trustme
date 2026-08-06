// Extracted from library/std/src/io/mod.rs:1613
A trait for objects which are byte-oriented sinks.

Implementors of the `Write` trait are sometimes called 'writers'.

Writers are defined by two required methods, [`write`] and [`flush`]:

* The [`write`] method will attempt to write some data into the object,
  returning how many bytes were successfully written.

* The [`flush`] method is useful for adapters and explicit buffers
  themselves for ensuring that all buffered data has been pushed out to the
  'true sink'.

Writers are intended to be composable with one another. Many implementors
throughout [`std::io`] take and provide types which implement the `Write`
trait.

[`write`]: Write::write
[`flush`]: Write::flush
[`std::io`]: self

Examples

```no_run
use std::io::prelude::*;
use std::fs::File;

fn main() -> std::io::Result<()> {
    let data = b"some bytes";

    let mut pos = 0;
    let mut buffer = File::create("foo.txt")?;

    while pos < data.len() {
        let bytes_written = buffer.write(&data[pos..])?;
        pos += bytes_written;
    }
    Ok(())
}
