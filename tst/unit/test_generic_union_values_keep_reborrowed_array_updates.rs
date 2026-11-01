//@ compile-variant: -O

union Storage<T: Copy> { value: T, fallback: u32 }
#[inline(never)]
fn update(bytes: [u8; 4], index: usize, replacement: u8) -> u32 {
    let mut storage = Storage { value: bytes };
    unsafe {
        let field = &mut storage.value;
        field[index] = replacement;
        storage.fallback
    }
}
fn main() {
    assert_eq!(update([3, 5, 7, 11], 2, 13), u32::from_ne_bytes([3, 5, 13, 11]));
    assert_eq!(update([17, 19, 23, 29], 0, 31), u32::from_ne_bytes([31, 19, 23, 29]));
}
