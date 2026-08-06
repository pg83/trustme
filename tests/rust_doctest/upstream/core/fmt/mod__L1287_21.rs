// Extracted from library/core/src/fmt/mod.rs:1287
#![allow(unused)]
fn main() {
    use std::fmt;
    
    struct Length(i32);
    
    impl fmt::Pointer for Length {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            // use `as` to convert to a `*const T`, which implements Pointer, which we can use
    
            let ptr = self as *const Self;
            fmt::Pointer::fmt(&ptr, f)
        }
    }
    
    let l = Length(42);
    
    println!("l is in memory here: {l:p}");
    
    let l_ptr = format!("{l:018p}");
    assert_eq!(l_ptr.len(), 18);
    assert_eq!(&l_ptr[..2], "0x");
}
