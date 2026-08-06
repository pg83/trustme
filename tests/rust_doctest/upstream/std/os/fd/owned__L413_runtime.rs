// Extracted from library/std/src/os/fd/owned.rs:413
#![allow(unused)]
fn main() {
    #[cfg(any(unix, target_os = "wasi"))] mod group_cfg {
    #[cfg(target_os = "wasi")]
    use std::os::wasi::io::AsFd;
    #[cfg(unix)]
    use std::os::unix::io::AsFd;
    use std::net::UdpSocket;
    use std::sync::Arc;
    
    trait MyTrait: AsFd {}
    impl MyTrait for Arc<UdpSocket> {}
    impl MyTrait for Box<UdpSocket> {}
    }
}
