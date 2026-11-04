//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Cpanic=unwind -Zmir-opt-level=2
//@ compile-variant: -O

extern "Rust" {
    fn action();
    fn cleanup();
}

struct Guard;

impl Drop for Guard {
    #[inline(always)]
    fn drop(&mut self) { unsafe { cleanup(); } }
}

#[inline(always)]
fn guarded() {
    let _guard = Guard;
    unsafe { action(); }
}

pub extern "C" fn boundary() { guarded(); }
