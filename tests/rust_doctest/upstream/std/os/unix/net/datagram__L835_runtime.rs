// Extracted from library/std/src/os/unix/net/datagram.rs:835
Returns the value of the `SO_ERROR` option.

Examples

```no_run
use std::os::unix::net::UnixDatagram;

fn main() -> std::io::Result<()> {
    let sock = UnixDatagram::unbound()?;
    if let Ok(Some(err)) = sock.take_error() {
        println!("Got error: {err:?}");
    }
    Ok(())
}
