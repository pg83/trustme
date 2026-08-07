// Extracted from library/core/src/ptr/mod.rs:480
#![allow(unused)]
fn main() {
    use std::ptr;

    /// Moves all the elements of `src` into `dst`, leaving `src` empty.
    fn append<T>(dst: &mut Vec<T>, src: &mut Vec<T>) {
        let src_len = src.len();
        let dst_len = dst.len();

        // Ensure that `dst` has enough capacity to hold all of `src`.
        dst.reserve(src_len);

        unsafe {
            // The call to add is always safe because `Vec` will never
            // allocate more than `isize::MAX` bytes.
            let dst_ptr = dst.as_mut_ptr().add(dst_len);
            let src_ptr = src.as_ptr();

            // Truncate `src` without dropping its contents. We do this first,
            // to avoid problems in case something further down panics.
            src.set_len(0);

            // The two regions cannot overlap because mutable references do
            // not alias, and two different vectors cannot own the same
            // memory.
            ptr::copy_nonoverlapping(src_ptr, dst_ptr, src_len);

            // Notify `dst` that it now holds the contents of `src`.
            dst.set_len(dst_len + src_len);
        }
    }

    let mut a = vec!['r'];
    let mut b = vec!['u', 's', 't'];

    append(&mut a, &mut b);

    assert_eq!(a, &['r', 'u', 's', 't']);
    assert!(b.is_empty());
}
