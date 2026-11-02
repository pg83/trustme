//@ compile-variant: -O
use std::sync::atomic::{AtomicU64, Ordering};
static DROPS: AtomicU64 = AtomicU64::new(0);
struct Mark(u8);
impl Drop for Mark {
    fn drop(&mut self) { DROPS.fetch_add(1u64 << self.0, Ordering::SeqCst); }
}
fn run(present: bool, base: u8) {
    let mut slot;
    if present { slot = Box::new(Mark(base)); drop(*slot); }
    let mut round = 0;
    loop {
        slot = Box::new(Mark(base + round + 1));
        if std::hint::black_box(round == 0) { drop(slot); } else { drop(*slot); }
        round += 1;
        if round == 2 { break; }
    }
}
fn main() { run(true, 0); run(false, 3); assert_eq!(DROPS.load(Ordering::SeqCst), 55); }
