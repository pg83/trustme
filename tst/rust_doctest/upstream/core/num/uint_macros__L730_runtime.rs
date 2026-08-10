// Extracted from library/core/src/num/uint_macros.rs:730
#![allow(unused)]
fn main() {
    let foo = 30_u32;
    let bar = 20;
    if foo >= bar {
        // SAFETY: just checked it will not overflow
        let diff = unsafe { foo.unchecked_sub(bar) };
        // ... use diff ...
    }
}
