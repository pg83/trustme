//@ compile-variant: -O
use std::sync::atomic::{AtomicU64, Ordering};
static DROPS: AtomicU64 = AtomicU64::new(0);
struct Mark(u8);
impl Drop for Mark {
    fn drop(&mut self) { DROPS.fetch_add(1u64 << self.0, Ordering::SeqCst); }
}
fn main() {
    {
        let mut slots = [const { Mark(0) }; 32];
        let [first, ..] = slots;
        drop(first);
        let mut round = 0;
        loop {
            slots = [const { Mark(1) }; 32];
            let [first, ..] = slots;
            drop(first);
            round += 1;
            if round == 2 { break; }
        }
    }
    assert_eq!(DROPS.load(Ordering::SeqCst), 160);
}
