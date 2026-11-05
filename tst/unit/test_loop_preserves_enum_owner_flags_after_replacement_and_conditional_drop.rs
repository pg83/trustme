use std::sync::atomic::{AtomicU64, Ordering};

static DROPS: AtomicU64 = AtomicU64::new(0);
enum Slot { A(u8), B(u8) }
impl Drop for Slot {
    fn drop(&mut self) {
        let id = match self { Slot::A(id) | Slot::B(id) => *id };
        let bit = 1u64 << id;
        assert_eq!(DROPS.fetch_or(bit, Ordering::SeqCst) & bit, 0);
    }
}
fn make(id: u8) -> Slot { Slot::A(id) }
fn forward(value: Slot) -> Slot { value }
fn replace_live() {
    let mut slot = make(0);
    let mut step = 1;
    loop {
        if std::hint::black_box(step % 2 == 0) { slot = forward(slot); }
        slot = Slot::A(step);
        step += 1;
        if step == 4 { break; }
    }
    assert_eq!(DROPS.load(Ordering::SeqCst), 7);
    drop(slot);
    assert_eq!(DROPS.load(Ordering::SeqCst), 15);
}
fn replace_maybe_dead() {
    let mut slot = make(8);
    let mut step = 0;
    loop {
        slot = Slot::B(9 + step);
        if std::hint::black_box(step % 2 == 0) { drop(slot); }
        step += 1;
        if step == 3 { break; }
    }
}
fn main() {
    replace_live();
    replace_maybe_dead();
    assert_eq!(DROPS.load(Ordering::SeqCst), 0x0f0f);
}
