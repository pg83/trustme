//@ compile-variant: -O
use std::sync::atomic::{AtomicU64, Ordering};
static DROPS: AtomicU64 = AtomicU64::new(0);
struct Mark(u8);
impl Drop for Mark {
    fn drop(&mut self) { DROPS.fetch_add(1u64 << self.0, Ordering::SeqCst); }
}
fn main() {
    {
        let mut slot = Box::new(Mark(0));
        drop(*slot);
        let mut round = 0;
        loop {
            slot = Box::new(Mark(round + 1));
            round += 1;
            if round == 2 { break; }
        }
    }
    assert_eq!(DROPS.load(Ordering::SeqCst), 7);
}
