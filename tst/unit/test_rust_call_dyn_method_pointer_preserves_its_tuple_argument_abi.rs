#![feature(unsized_fn_params, unboxed_closures)]
#![allow(incomplete_features)]
use std::sync::atomic::{AtomicUsize, Ordering};
static DROPS: AtomicUsize = AtomicUsize::new(0);
struct Token(u16);
impl Drop for Token {
    fn drop(&mut self) { DROPS.fetch_add(1, Ordering::SeqCst); }
}
trait Extract { extern "rust-call" fn extract(self, args: (u16, u16)) -> u16; }
impl Extract for Token {
    extern "rust-call" fn extract(self, args: (u16, u16)) -> u16 {
        self.0 + args.0 * 2 + args.1 * 3
    }
}
fn main() {
    let function: extern "rust-call" fn(dyn Extract, (u16, u16)) -> u16 = <dyn Extract as Extract>::extract;
    let function = std::hint::black_box(function);
    let value: Box<dyn Extract> = Box::new(Token(11));
    assert_eq!(function(*value, (5, 7)), 42);
    assert_eq!(DROPS.load(Ordering::SeqCst), 1);
}
