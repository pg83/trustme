// Extracted from library/core/src/ptr/mod.rs:1269
#![allow(unused)]
fn main() {
    use std::ptr;
    
    let mut array: [i32; 4] = [0, 1, 2, 3];
    
    let array_ptr: *mut i32 = array.as_mut_ptr();
    
    let x = array_ptr as *mut [i32; 3]; // this is `array[0..3]`
    let y = unsafe { array_ptr.add(1) } as *mut [i32; 3]; // this is `array[1..4]`
    
    unsafe {
        ptr::swap(x, y);
        // The indices `1..3` of the slice overlap between `x` and `y`.
        // Reasonable results would be for to them be `[2, 3]`, so that indices `0..3` are
        // `[1, 2, 3]` (matching `y` before the `swap`); or for them to be `[0, 1]`
        // so that indices `1..4` are `[0, 1, 2]` (matching `x` before the `swap`).
        // This implementation is defined to make the latter choice.
        assert_eq!([1, 0, 1, 2], array);
    }
}
