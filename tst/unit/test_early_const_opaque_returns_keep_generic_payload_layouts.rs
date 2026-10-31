const fn hidden<T: Copy>(value: T) -> impl Copy { value }
const fn length() -> usize {
    let array = hidden([17u16; 3]);
    let pair = hidden((23u64, 41u64));
    std::mem::size_of_val(&array) + std::mem::size_of_val(&pair)
}
type Selected = [u8; length()];
fn main() {
    let value: Selected = [3; 22];
    assert_eq!(value, [3; 22]);
    assert_eq!(std::mem::size_of_val(&hidden([17u32; 3])), 12);
}
