// Extracted from library/core/src/ptr/mod.rs:2007
#![allow(unused)]
fn main() {
    fn write_usize(x: &mut [u8], val: usize) {
        assert!(x.len() >= size_of::<usize>());
    
        let ptr = x.as_mut_ptr() as *mut usize;
    
        unsafe { ptr.write_unaligned(val) }
    }
}
