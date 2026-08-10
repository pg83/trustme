// Extracted from library/core/src/convert/mod.rs:66
#![allow(unused)]
fn main() {
    use std::convert::identity;

    fn manipulation(x: u32) -> u32 {
        // Let's pretend that adding one is an interesting function.
        x + 1
    }

    let _arr = &[identity, manipulation];
}
