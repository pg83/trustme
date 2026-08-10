// Extracted from library/std/src/os/unix/net/datagram.rs:803
use std::os::unix::net::UnixDatagram;

fn main() -> std::io::Result<()> {
    let sock = UnixDatagram::unbound()?;
    sock.set_nonblocking(true).expect("set_nonblocking function failed");
    Ok(())
}
