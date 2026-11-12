#![feature(arbitrary_self_types)]

use std::ops::Receiver;

struct Counter(u16);
struct Small(Counter);
struct Large([u8; 17], Counter);
impl Receiver for Small { type Target = Counter; }
impl Receiver for Large { type Target = Counter; }
impl Counter {
    fn receiver_bytes(self: &dyn Receiver<Target = Self>) -> usize {
        std::mem::size_of_val(self)
    }
}

fn main() {
    let small = Small(Counter(17));
    let large = Large([1; 17], Counter(23));
    let first: &dyn Receiver<Target = Counter> = &small;
    let second: &dyn Receiver<Target = Counter> = &large;
    assert_ne!(std::mem::size_of::<Small>(), std::mem::size_of::<Large>());
    assert_eq!(first.receiver_bytes(), std::mem::size_of::<Small>());
    assert_eq!(Counter::receiver_bytes(second), std::mem::size_of::<Large>());
}
