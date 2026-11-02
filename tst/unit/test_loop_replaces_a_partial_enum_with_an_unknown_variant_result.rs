//@ compile-variant: -O
use std::sync::atomic::{AtomicU64, Ordering};
static DROPS: AtomicU64 = AtomicU64::new(0);
struct Mark(u8);
impl Drop for Mark {
    fn drop(&mut self) { DROPS.fetch_add(1u64 << self.0, Ordering::SeqCst); }
}
enum Slot { Full { left: Mark, right: Mark }, Empty }
#[inline(never)]
fn full(left: u8, right: u8) -> Slot { Slot::Full { left: Mark(left), right: Mark(right) } }
fn run(present: bool, base: u8) {
    let mut slot;
    if present {
        slot = Slot::Full { left: Mark(base), right: Mark(base + 1) };
        if let Slot::Full { left, .. } = slot { drop(left); }
    }
    let mut round = 0;
    loop {
        slot = full(base + 2 + round * 2, base + 3 + round * 2);
        round += 1;
        if round == 2 { break; }
    }
}
fn main() { run(true, 0); run(false, 6); assert_eq!(DROPS.load(Ordering::SeqCst), 3903); }
