// Extracted from library/core/src/primitive_docs.rs:894
#![allow(unused)]
fn main() {
    use std::rc::Rc;
    let pointer_size = size_of::<&u8>();
    assert_eq!(2 * pointer_size, size_of::<&[u8]>());
    assert_eq!(2 * pointer_size, size_of::<*const [u8]>());
    assert_eq!(2 * pointer_size, size_of::<Box<[u8]>>());
    assert_eq!(2 * pointer_size, size_of::<Rc<[u8]>>());
}
