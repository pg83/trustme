use std::sync::atomic::{AtomicUsize, Ordering};
static DROPS: AtomicUsize = AtomicUsize::new(0);
trait Consume { fn consume(self: Box<Self>) -> u32; }
struct Item { value: u32 }
impl Drop for Item { fn drop(&mut self) { DROPS.fetch_add(1, Ordering::SeqCst); } }
impl Consume for Item { fn consume(self: Box<Self>) -> u32 { self.value + 19 } }
fn main() {
    let value: Box<dyn Consume> = Box::new(Item { value: 283 });
    let call: fn(Box<dyn Consume>) -> u32 = <dyn Consume as Consume>::consume;
    assert_eq!(std::hint::black_box(call)(value), 302);
    assert_eq!(DROPS.load(Ordering::SeqCst), 1);
}
