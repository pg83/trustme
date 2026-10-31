#![feature(ptr_metadata)]
#[inline(never)]
fn inspect(value: Box<[u32]>) -> (usize, u32, usize) {
    let pointer: *const [u32] = &*value;
    let data = pointer as *const u32;
    let size = std::mem::size_of_val(&*value);
    (std::ptr::metadata(pointer), unsafe { *data.add(1) }, size)
}
fn main() {
    assert_eq!(inspect(vec![37, 41, 43].into_boxed_slice()), (3, 41, 12));
    assert_eq!(inspect(vec![47, 53].into_boxed_slice()), (2, 53, 8));
}
