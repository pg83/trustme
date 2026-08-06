// Extracted from library/core/src/sync/atomic.rs:3287
#![allow(unused)]
fn main() {
    assert_eq!(foo.fetch_or(0b110011, Ordering::SeqCst), 0b101101);
    assert_eq!(foo.load(Ordering::SeqCst), 0b111111);
}
