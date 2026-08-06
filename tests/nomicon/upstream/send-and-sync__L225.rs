// Extracted from src/send-and-sync.md:225
#![allow(unused)]
fn main() {
    struct Carton<T>(std::ptr::NonNull<T>);
    mod libc {
        pub use ::std::os::raw::c_void;
        unsafe extern "C" { pub fn free(p: *mut c_void); }
    }
    impl<T> Drop for Carton<T> {
        fn drop(&mut self) {
            unsafe {
                libc::free(self.0.as_ptr().cast());
            }
        }
    }
}
