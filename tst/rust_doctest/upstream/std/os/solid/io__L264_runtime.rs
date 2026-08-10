// Extracted from library/std/src/os/solid/io.rs:264
#![allow(unused)]
fn main() {
    #[cfg(target_os = "solid_asp3")] mod group_cfg {
    use std::os::solid::io::AsFd;
    use std::net::UdpSocket;
    use std::sync::Arc;

    trait MyTrait: AsFd {}
    impl MyTrait for Arc<UdpSocket> {}
    impl MyTrait for Box<UdpSocket> {}
    }
}
