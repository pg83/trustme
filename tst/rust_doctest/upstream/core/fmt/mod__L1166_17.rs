// Extracted from library/core/src/fmt/mod.rs:1166
#![allow(unused)]
fn main() {
    let y = 42; // 42 is '2a' in hex

    assert_eq!(format!("{y:x}"), "2a");
    assert_eq!(format!("{y:#x}"), "0x2a");

    assert_eq!(format!("{:x}", -16), "fffffff0");
}
