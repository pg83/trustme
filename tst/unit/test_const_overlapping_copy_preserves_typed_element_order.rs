const fn shifted() -> [u16; 5] {
    let mut values = [13u16, 17, 19, 23, 29];
    unsafe { core::ptr::copy(values.as_ptr(), values.as_mut_ptr().add(1), 4); }
    values
}
const VALUE: [u16; 5] = shifted();
fn main() { assert_eq!(VALUE, [13, 13, 17, 19, 23]); }
