#![feature(unsized_fn_params)]
#![allow(incomplete_features)]
use std::sync::atomic::{AtomicUsize, Ordering};
static DROPS: AtomicUsize = AtomicUsize::new(0);
struct Token(u16);
impl Drop for Token {
    fn drop(&mut self) { DROPS.fetch_add(1, Ordering::SeqCst); }
}
trait Extract { fn extract(self) -> u16; }
impl Extract for Token { fn extract(self) -> u16 { self.0 } }
fn main() {
    let function: fn(dyn Extract) -> u16 = <dyn Extract as Extract>::extract;
    let function = std::hint::black_box(function);
    let value: Box<dyn Extract> = Box::new(Token(37));
    assert_eq!(function(*value), 37);
    assert_eq!(DROPS.load(Ordering::SeqCst), 1);
}
