// Extracted from src/expressions/operator-expr.md:141
#![allow(unused)]
fn main() {
    use std::mem::MaybeUninit;
    
    struct Demo {
        field: bool,
    }
    
    let mut uninit = MaybeUninit::<Demo>::uninit();
    // `&uninit.as_mut().field` would create a reference to an uninitialized `bool`,
    // and thus be undefined behavior!
    let f1_ptr = unsafe { &raw mut (*uninit.as_mut_ptr()).field };
    unsafe { f1_ptr.write(true); }
    let init = unsafe { uninit.assume_init() };
}
