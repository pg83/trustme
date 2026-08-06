// Extracted from library/std/src/os/unix/net/addr.rs:73
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::os::unix::net::UnixListener;
        
        let socket = match UnixListener::bind("/tmp/sock") {
            Ok(sock) => sock,
            Err(e) => {
                println!("Couldn't bind: {e:?}");
                return
            }
        };
        let addr = socket.local_addr().expect("Couldn't get local address");
        Ok(())
    }
    doctest().unwrap();
}
