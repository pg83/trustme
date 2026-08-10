// Extracted from library/core/src/num/uint_macros.rs:742
#![allow(unused)]
fn main() {
    let foo = 30_u32;
    let bar = 20;
    if let Some(diff) = foo.checked_sub(bar) {
        // ... use diff ...
    }
}
