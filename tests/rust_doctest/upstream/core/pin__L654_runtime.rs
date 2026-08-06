// Extracted from library/core/src/pin.rs:654
#![allow(unused)]
fn main() {
    use std::pin::Pin;
    use std::marker::PhantomPinned;
    use std::ptr::NonNull;
    struct Unmovable {
        data: [u8; 64],
        slice: NonNull<[u8]>,
        _pin: PhantomPinned,
    }
    
    impl Unmovable {
        // Copies the contents of `src` into `self`, fixing up the self-pointer
        // in the process.
        fn assign(self: Pin<&mut Self>, src: Pin<&mut Self>) {
            unsafe {
                let unpinned_self = Pin::into_inner_unchecked(self);
                let unpinned_src = Pin::into_inner_unchecked(src);
                *unpinned_self = Self {
                    data: unpinned_src.data,
                    slice: NonNull::from(&mut []),
                    _pin: PhantomPinned,
                };
    
                let data_ptr = unpinned_src.data.as_ptr() as *const u8;
                let slice_ptr = unpinned_src.slice.as_ptr() as *const u8;
                let offset = slice_ptr.offset_from(data_ptr) as usize;
                let len = unpinned_src.slice.as_ptr().len();
    
                unpinned_self.slice = NonNull::from(&mut unpinned_self.data[offset..offset+len]);
            }
        }
    }
}
