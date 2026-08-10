// Extracted from library/std/src/os/fd/raw.rs:243
#![allow(unused)]
fn main() {
    #[cfg(any(unix, target_os = "wasi"))] mod group_cfg {
    #[cfg(target_os = "wasi")]
    use std::os::wasi::io::AsRawFd;
    #[cfg(unix)]
    use std::os::unix::io::AsRawFd;
    use std::net::UdpSocket;
    use std::sync::Arc;
    trait MyTrait: AsRawFd {
    }
    impl MyTrait for Arc<UdpSocket> {}
    impl MyTrait for Box<UdpSocket> {}
    }
}
