#![allow(dead_code)]
struct SizedByClosure([u8; {
    let closure = || 41u8;
    std::mem::size_of_val(&closure) + std::mem::align_of_val(&closure) + 2
}]);
fn main() {
    let value = SizedByClosure([7; 3]);
    assert_eq!(value.0, [7; 3]);
    assert_eq!(std::mem::size_of::<SizedByClosure>(), 3);
}
