// Extracted from library/std/src/sync/mod.rs:12
// FIXME(static_mut_refs): Do not allow `static_mut_refs` lint
#![allow(static_mut_refs)]

static mut A: u32 = 0;
static mut B: u32 = 0;
static mut C: u32 = 0;

fn main() {
    unsafe {
        A = 3;
        B = 4;
        A = A + B;
        C = B;
        println!("{A} {B} {C}");
        C = A;
    }
}
