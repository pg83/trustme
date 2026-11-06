#![feature(const_drop_in_place, const_destruct)]
struct Width([u8; {
    let mut action = || 47u8;
    let width = std::mem::size_of_val(&action) + std::mem::align_of_val(&action) + 4;
    unsafe { std::ptr::drop_in_place(&mut action); }
    width
}]);
fn main() {
    let value = Width([11; 5]);
    assert_eq!(value.0, [11; 5]);
    assert_eq!(std::mem::size_of::<Width>(), 5);
}
