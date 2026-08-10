// Extracted from library/std/src/os/unix/net/addr.rs:223
use std::os::unix::net::UnixDatagram;

fn main() -> std::io::Result<()> {
    let socket = UnixDatagram::unbound()?;
    let addr = socket.local_addr().expect("Couldn't get local address");
    assert_eq!(addr.as_pathname(), None);
    Ok(())
}
