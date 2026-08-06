// Extracted from library/core/src/ptr/mod.rs:1622
#![allow(unused)]
fn main() {
    use std::ptr;
    
    fn swap<T>(a: &mut T, b: &mut T) {
        unsafe {
            // Create a bitwise copy of the value at `a` in `tmp`.
            let tmp = ptr::read(a);
    
            // Exiting at this point (either by explicitly returning or by
            // calling a function which panics) would cause the value in `tmp` to
            // be dropped while the same value is still referenced by `a`. This
            // could trigger undefined behavior if `T` is not `Copy`.
    
            // Create a bitwise copy of the value at `b` in `a`.
            // This is safe because mutable references cannot alias.
            ptr::copy_nonoverlapping(b, a, 1);
    
            // As above, exiting here could trigger undefined behavior because
            // the same value is referenced by `a` and `b`.
    
            // Move `tmp` into `b`.
            ptr::write(b, tmp);
    
            // `tmp` has been moved (`write` takes ownership of its second argument),
            // so nothing is dropped implicitly here.
        }
    }
    
    let mut foo = "foo".to_owned();
    let mut bar = "bar".to_owned();
    
    swap(&mut foo, &mut bar);
    
    assert_eq!(foo, "bar");
    assert_eq!(bar, "foo");
}
