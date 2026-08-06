// Extracted from library/alloc/src/fmt.rs:184
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(format!("Hello {:+}!", 5), "Hello +5!");
    assert_eq!(format!("{:#x}!", 27), "0x1b!");
    assert_eq!(format!("Hello {:05}!", 5),  "Hello 00005!");
    assert_eq!(format!("Hello {:05}!", -5), "Hello -0005!");
    assert_eq!(format!("{:#010x}!", 27), "0x0000001b!");
}
