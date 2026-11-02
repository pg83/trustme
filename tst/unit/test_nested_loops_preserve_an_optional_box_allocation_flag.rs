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
    let mut outer = 0;
    loop {
        let mut inner = 0;
        loop {
            if outer == 0 && inner == 0 { slot = Box::new(Mark(base + 1)); drop(*slot); }
            inner += 1;
            if inner == 2 { break; }
        }
        outer += 1;
        if outer == 2 { break; }
    }
}
fn main() { run(true, 0); run(false, 3); assert_eq!(DROPS.load(Ordering::SeqCst), 19); }
