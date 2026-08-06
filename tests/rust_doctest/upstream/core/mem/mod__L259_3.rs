// Extracted from library/core/src/mem/mod.rs:259
#![allow(unused)]
fn main() {
    // Some primitives
    assert_eq!(4, size_of::<i32>());
    assert_eq!(8, size_of::<f64>());
    assert_eq!(0, size_of::<()>());
    
    // Some arrays
    assert_eq!(8, size_of::<[i32; 2]>());
    assert_eq!(12, size_of::<[i32; 3]>());
    assert_eq!(0, size_of::<[i32; 0]>());
    
    
    // Pointer size equality
    assert_eq!(size_of::<&i32>(), size_of::<*const i32>());
    assert_eq!(size_of::<&i32>(), size_of::<Box<i32>>());
    assert_eq!(size_of::<&i32>(), size_of::<Option<&i32>>());
    assert_eq!(size_of::<Box<i32>>(), size_of::<Option<Box<i32>>>());
}
