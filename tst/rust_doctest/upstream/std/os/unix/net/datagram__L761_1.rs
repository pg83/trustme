// Extracted from library/std/src/os/unix/net/datagram.rs:761
use std::os::unix::net::UnixDatagram;
use std::time::Duration;

fn main() -> std::io::Result<()> {
    let sock = UnixDatagram::unbound()?;
    sock.set_read_timeout(Some(Duration::new(1, 0)))
        .expect("set_read_timeout function failed");
    assert_eq!(sock.read_timeout()?, Some(Duration::new(1, 0)));
    Ok(())
}
