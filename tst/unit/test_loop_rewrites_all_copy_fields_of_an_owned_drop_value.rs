use std::sync::atomic::{AtomicUsize, Ordering};

static DROPS: AtomicUsize = AtomicUsize::new(0);
struct Packet(u32, u32);
impl Drop for Packet {
    fn drop(&mut self) {
        assert_eq!(self.1, self.0 + 100);
        DROPS.fetch_add(1, Ordering::SeqCst);
    }
}
fn forward(value: Packet) -> Packet { value }
fn main() {
    let mut value = Packet(0, 100);
    let mut step = 0;
    loop {
        if std::hint::black_box(step % 2 == 0) {
            value = forward(value);
        }
        value.0 = step;
        value.1 = step + 100;
        step += 1;
        if step == 4 { break; }
    }
    assert_eq!((value.0, value.1), (3, 103));
    assert_eq!(DROPS.load(Ordering::SeqCst), 0);
    drop(value);
    assert_eq!(DROPS.load(Ordering::SeqCst), 1);
}
