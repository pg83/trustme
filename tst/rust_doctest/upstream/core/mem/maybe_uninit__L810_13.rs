// Extracted from library/core/src/mem/maybe_uninit.rs:810
#![allow(unused)]
#![allow(unexpected_cfgs)]
fn main() {
    use std::mem::MaybeUninit;

    unsafe extern "C" fn initialize_buffer(buf: *mut [u8; 1024]) { unsafe { *buf = [0; 1024] } }
    #[cfg(FALSE)]
    extern "C" {
        /// Initializes *all* the bytes of the input buffer.
        fn initialize_buffer(buf: *mut [u8; 1024]);
    }

    let mut buf = MaybeUninit::<[u8; 1024]>::uninit();

    // Initialize `buf`:
    unsafe { initialize_buffer(buf.as_mut_ptr()); }
    // Now we know that `buf` has been initialized, so we could `.assume_init()` it.
    // However, using `.assume_init()` may trigger a `memcpy` of the 1024 bytes.
    // To assert our buffer has been initialized without copying it, we upgrade
    // the `&mut MaybeUninit<[u8; 1024]>` to a `&mut [u8; 1024]`:
    let buf: &mut [u8; 1024] = unsafe {
        // SAFETY: `buf` has been initialized.
        buf.assume_init_mut()
    };

    // Now we can use `buf` as a normal slice:
    buf.sort_unstable();
    assert!(
        buf.windows(2).all(|pair| pair[0] <= pair[1]),
        "buffer is sorted",
    );
}
