// Extracted from src/vec/vec-drain.md:32
#![allow(unused)]
fn main() {
    struct RawValIter<T> {
        start: *const T,
        end: *const T,
    }
    
    impl<T> RawValIter<T> {
        // unsafe to construct because it has no associated lifetimes.
        // This is necessary to store a RawValIter in the same struct as
        // its actual allocation. OK since it's a private implementation
        // detail.
        unsafe fn new(slice: &[T]) -> Self {
            RawValIter {
                start: slice.as_ptr(),
                end: if slice.len() == 0 {
                    // if `len = 0`, then this is not actually allocated memory.
                    // Need to avoid offsetting because that will give wrong
                    // information to LLVM via GEP.
                    slice.as_ptr()
                } else {
                    slice.as_ptr().add(slice.len())
                }
            }
        }
    }
    
    // Iterator and DoubleEndedIterator impls identical to IntoIter.
}
