// `p as *mut _ as *mut u8` (alloc's rc.rs) left spare rules when the middle
// ivar had only cast bounds and no real coercion.
struct Inner<T: ?Sized> { _v: T }
fn addr<T: ?Sized>(p: *mut Inner<T>) -> usize {
    (p as *mut _ as *mut u8) as usize
}
fn main() {
    let mut b = Inner { _v: [1u8, 2, 3] };
    let p: *mut Inner<[u8; 3]> = &mut b;
    assert_eq!(addr(p), p as *mut u8 as usize);
}
