// Extracted from src/phantom-data.md:201
#![allow(unused)]
fn main() {
    #[cfg(any())]
    // we pinky-swear not to use `T` when dropping a `Vec`…
    unsafe impl<#[may_dangle] T> Drop for Vec<T> {
        fn drop(&mut self) {
            unsafe {
                if mem::needs_drop::<T>() {
                    /* … except here, that is, … */
                    ptr::drop_in_place::<[T]>(/* … */);
                }
                // …
                dealloc(/* … */)
                // …
            }
        }
    }
    
    struct Vec<T> {
        // … except for the fact that a `Vec` owns `T` items and
        // may thus be dropping `T` items on drop!
        _owns_T: core::marker::PhantomData<T>,
    
        ptr: *const T, // `*const` for variance (but this does not express ownership of a `T` *per se*)
        len: usize,
        cap: usize,
    }
}
