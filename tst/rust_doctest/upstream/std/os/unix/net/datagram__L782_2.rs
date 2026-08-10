// Extracted from library/std/src/os/unix/net/datagram.rs:782
use std::os::unix::net::UnixDatagram;
use std::time::Duration;

fn main() -> std::io::Result<()> {
    let sock = UnixDatagram::unbound()?;
    sock.set_write_timeout(Some(Duration::new(1, 0)))
        .expect("set_write_timeout function failed");
    assert_eq!(sock.write_timeout()?, Some(Duration::new(1, 0)));
    Ok(())
}
