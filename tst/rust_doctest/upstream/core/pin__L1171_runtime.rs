// Extracted from library/core/src/pin.rs:1171
#![allow(unused)]
fn main() {
    use std::pin::Pin;

    let mut val: u8 = 5;

    // Since `val` doesn't care about being moved, we can safely create a "facade" `Pin`
    // which will allow `val` to participate in `Pin`-bound apis  without checking that
    // pinning guarantees are actually upheld.
    let mut pinned: Pin<&mut u8> = Pin::new(&mut val);
}
