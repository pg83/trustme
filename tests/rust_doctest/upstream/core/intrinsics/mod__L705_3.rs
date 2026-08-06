// Extracted from library/core/src/intrinsics/mod.rs:705
#![allow(unused)]
fn main() {
    // this is not a good way to do this.
    let slice = unsafe { std::mem::transmute::<&str, &[u8]>("Rust") };
    assert_eq!(slice, &[82, 117, 115, 116]);
    
    // You could use `str::as_bytes`
    let slice = "Rust".as_bytes();
    assert_eq!(slice, &[82, 117, 115, 116]);
    
    // Or, just use a byte string, if you have control over the string
    // literal
    assert_eq!(b"Rust", &[82, 117, 115, 116]);
}
