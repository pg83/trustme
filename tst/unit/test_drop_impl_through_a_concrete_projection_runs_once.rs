//@ compile-fail: Drop can only be implemented for structs, enums, and unions
use std::sync::atomic::{AtomicU8, Ordering};
static COUNT: AtomicU8 = AtomicU8::new(0);
struct Payload;
trait Family { type Item; }
struct Kind;
impl Family for Kind { type Item = Payload; }
impl Drop for <Kind as Family>::Item { fn drop(&mut self) { COUNT.fetch_add(1, Ordering::SeqCst); } }
fn main() {
    drop(Payload);
    assert_eq!(COUNT.load(Ordering::SeqCst), 1);
}
