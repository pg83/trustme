// Extracted from library/std/src/os/unix/net/addr.rs:142
use std::os::unix::net::SocketAddr;
use std::path::Path;

fn main() -> std::io::Result<()> {
let address = SocketAddr::from_pathname("/path/to/socket")?;
assert_eq!(address.as_pathname(), Some(Path::new("/path/to/socket")));
Ok(())
}
