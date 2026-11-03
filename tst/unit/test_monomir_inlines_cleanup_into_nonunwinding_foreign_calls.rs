//@ compile-only
//@ compile-flags: -Ccodegen-type=monomir -Cpanic=unwind -Zmir-opt-level=2
//@ compile-variant: -O
use std::sync::atomic::{AtomicUsize, Ordering};

static EVENTS: AtomicUsize = AtomicUsize::new(0);
struct Guard(fn());
impl Drop for Guard {
    fn drop(&mut self) { (self.0)(); }
}

#[inline(always)]
fn guarded(body: fn(), cleanup: fn()) {
    let guard = Guard(cleanup);
    body();
    drop(guard);
}

#[inline(never)]
pub extern "C" fn boundary(body: fn(), cleanup: fn()) {
    guarded(body, cleanup);
}

fn body() { EVENTS.fetch_add(1, Ordering::SeqCst); }
fn cleanup() { EVENTS.fetch_add(10, Ordering::SeqCst); }
fn main() {
    boundary(std::hint::black_box(body), std::hint::black_box(cleanup));
    assert_eq!(EVENTS.load(Ordering::SeqCst), 11);
}
