// Extracted from library/core/src/panic/unwind_safe.rs:128
#![allow(unused)]
fn main() {
    use std::panic::{self, AssertUnwindSafe};
    
    let mut variable = 4;
    
    // This code will not compile because the closure captures `&mut variable`
    // which is not considered unwind safe by default.
    
    // panic::catch_unwind(|| {
    //     variable += 3;
    // });
    
    // This, however, will compile due to the `AssertUnwindSafe` wrapper
    let result = panic::catch_unwind(AssertUnwindSafe(|| {
        variable += 3;
    }));
    // ...
}
