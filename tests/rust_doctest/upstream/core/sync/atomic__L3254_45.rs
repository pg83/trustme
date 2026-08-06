// Extracted from library/core/src/sync/atomic.rs:3254
#![allow(unused)]
fn main() {
    assert_eq!(foo.fetch_nand(0x31, Ordering::SeqCst), 0x13);
    assert_eq!(foo.load(Ordering::SeqCst), !(0x13 & 0x31));
}
