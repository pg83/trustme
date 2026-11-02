//@ compile-variant: -O
use std::sync::atomic::{AtomicU64, Ordering};
static DROPS: AtomicU64 = AtomicU64::new(0);
struct Mark(u8);
impl Drop for Mark {
    fn drop(&mut self) { DROPS.fetch_add(1u64 << self.0, Ordering::SeqCst); }
}
fn main() {
    {
        let mut slots = [Mark(0), Mark(1), Mark(2)];
        let [first, _, _] = slots;
        drop(first);
        let mut round = 0;
        while round < 3 {
            if round == 1 { slots = [Mark(3), Mark(4), Mark(5)]; }
            round += 1;
        }
    }
    assert_eq!(DROPS.load(Ordering::SeqCst), 63);
}
