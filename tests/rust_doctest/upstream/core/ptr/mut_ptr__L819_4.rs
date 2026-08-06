// Extracted from library/core/src/ptr/mut_ptr.rs:819
#![allow(unused)]
fn main() {
    *Incorrect* usage:
    
    ```rust,no_run
    let ptr1 = Box::into_raw(Box::new(0u8));
    let ptr2 = Box::into_raw(Box::new(1u8));
    let diff = (ptr2 as isize).wrapping_sub(ptr1 as isize);
    // Make ptr2_other an "alias" of ptr2.add(1), but derived from ptr1.
    let ptr2_other = (ptr1 as *mut u8).wrapping_offset(diff).wrapping_offset(1);
    assert_eq!(ptr2 as usize, ptr2_other as usize);
    // Since ptr2_other and ptr2 are derived from pointers to different objects,
    // computing their offset is undefined behavior, even though
    // they point to addresses that are in-bounds of the same object!
    unsafe {
        let one = ptr2_other.offset_from(ptr2); // Undefined Behavior! ⚠️
    }
}
