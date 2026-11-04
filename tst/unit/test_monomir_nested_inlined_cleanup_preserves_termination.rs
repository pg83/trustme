//@ compile-only
//@ compile-flags: -Ccodegen-type=monomir -Cpanic=unwind -Zmir-opt-level=2
//@ compile-variant: -O
use std::sync::atomic::{AtomicUsize, Ordering};

static EVENTS: AtomicUsize = AtomicUsize::new(0);
struct Inner(fn());
impl Drop for Inner {
    #[inline(always)]
    fn drop(&mut self) { (self.0)(); }
}
struct Outer(fn(), fn());
impl Drop for Outer {
    #[inline(always)]
    fn drop(&mut self) {
        let _inner = Inner(self.1);
        (self.0)();
    }
}

#[inline(always)]
fn guarded(body: fn(), first: fn(), second: fn()) {
    let _outer = Outer(first, second);
    body();
}

#[inline(never)]
pub extern "C" fn boundary(body: fn(), first: fn(), second: fn()) {
    guarded(body, first, second);
}

fn body() { EVENTS.fetch_add(1, Ordering::SeqCst); }
fn first() { EVENTS.fetch_add(10, Ordering::SeqCst); }
fn second() { EVENTS.fetch_add(100, Ordering::SeqCst); }
fn main() {
    boundary(std::hint::black_box(body), std::hint::black_box(first), std::hint::black_box(second));
    assert_eq!(EVENTS.load(Ordering::SeqCst), 111);
}
