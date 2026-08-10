// Extracted from library/std/src/os/windows/io/handle.rs:463
#![allow(unused)]
fn main() {
    #[cfg(windows)] mod group_cfg {
    use std::os::windows::io::AsHandle;
    use std::fs::File;
    use std::sync::Arc;

    trait MyTrait: AsHandle {}
    impl MyTrait for Arc<File> {}
    impl MyTrait for Box<File> {}
    }
}
