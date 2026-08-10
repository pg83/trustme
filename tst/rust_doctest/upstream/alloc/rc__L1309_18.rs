// Extracted from library/alloc/src/rc.rs:1309
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let x: Rc<[u32]> = Rc::new([1, 2, 3]);
    let x_ptr: *const [u32] = Rc::into_raw(x);

    unsafe {
        let x: Rc<[u32; 3]> = Rc::from_raw(x_ptr.cast::<[u32; 3]>());
        assert_eq!(&*x, &[1, 2, 3]);
    }
}
