// Extracted from library/std/src/os/unix/net/stream.rs:425
Returns the value of the `SO_ERROR` option.

Examples

```no_run
use std::os::unix::net::UnixStream;

fn main() -> std::io::Result<()> {
    let socket = UnixStream::connect("/tmp/sock")?;
    if let Ok(Some(err)) = socket.take_error() {
        println!("Got error: {err:?}");
    }
    Ok(())
}
