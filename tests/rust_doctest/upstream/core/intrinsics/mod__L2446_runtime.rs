// Extracted from library/core/src/intrinsics/mod.rs:2446
#![allow(unused)]
#![feature(core_intrinsics)]
#![allow(internal_features)]
fn main() {
    use std::intrinsics::is_val_statically_known;
    
    fn foo(x: &i32) -> bool {
        is_val_statically_known(x)
    }
    
    fn bar(x: &i32) -> bool {
        is_val_statically_known(
            (x as *const i32).addr()
        )
    }
    _ = foo(&5_i32);
    _ = bar(&5_i32);
}
