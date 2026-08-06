// Extracted from library/std/src/os/windows/io/socket.rs:244
#![allow(unused)]
fn main() {
    #[cfg(windows)] mod group_cfg {
    use std::os::windows::io::AsSocket;
    use std::net::UdpSocket;
    use std::sync::Arc;
    
    trait MyTrait: AsSocket {}
    impl MyTrait for Arc<UdpSocket> {}
    impl MyTrait for Box<UdpSocket> {}
    }
}
