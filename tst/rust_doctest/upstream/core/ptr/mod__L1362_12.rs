// Extracted from library/core/src/ptr/mod.rs:1362
#![allow(unused)]
fn main() {
    use std::mem::size_of;
    use std::ptr;

    const { unsafe {
        const PTR_SIZE: usize = size_of::<*const i32>();
        let mut data1 = [0u8; PTR_SIZE];
        let mut data2 = [0u8; PTR_SIZE];
        // Store a pointer in `data1`.
        data1.as_mut_ptr().cast::<*const i32>().write_unaligned(&42);
        // Swap the contents of `data1` and `data2` by swapping `PTR_SIZE` many `u8`-sized chunks.
        // This call will fail, because the pointer in `data1` crosses the boundary
        // between several of the 1-byte chunks that are being swapped here.
        //ptr::swap_nonoverlapping(data1.as_mut_ptr(), data2.as_mut_ptr(), PTR_SIZE);
        // Swap the contents of `data1` and `data2` by swapping a single chunk of size
        // `[u8; PTR_SIZE]`. That works, as there is no pointer crossing the boundary between
        // two chunks.
        ptr::swap_nonoverlapping(&mut data1, &mut data2, 1);
        // Read the pointer from `data2` and dereference it.
        let ptr = data2.as_ptr().cast::<*const i32>().read_unaligned();
        assert!(*ptr == 42);
    } }
}
