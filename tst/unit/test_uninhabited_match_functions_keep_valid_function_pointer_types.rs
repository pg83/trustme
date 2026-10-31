#![feature(never_type)]
enum Void {}
unsafe fn from_pointer(value: *const !) -> u8 { match *value {} }
fn from_empty(value: Void) -> u8 { match value {} }
fn main() {
    let pointer: unsafe fn(*const !) -> u8 = from_pointer;
    let empty: fn(Void) -> u8 = from_empty;
    assert!(pointer as usize != 0);
    assert!(empty as usize != 0);
}
