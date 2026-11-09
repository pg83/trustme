#[inline(never)]
fn select<const N: u8>(mut value: u32) -> u32 {
    let condition = N == 7;
    value ^= 3;
    if condition { value ^ 0x55 } else { value ^ 0xaa }
}

#[inline(never)]
fn overwrite(value: bool) -> u8 {
    let mut condition = true;
    *&mut condition = value;
    if condition { 29 } else { 43 }
}

#[inline(never)]
fn borrowed(value: bool) -> u8 {
    let mut condition = true;
    let pointer = &mut condition;
    *pointer = value;
    if condition { 47 } else { 61 }
}

fn main() {
    let value = std::hint::black_box(23);
    assert_eq!(select::<7>(value), (23 ^ 3) ^ 0x55);
    assert_eq!(select::<9>(value), (23 ^ 3) ^ 0xaa);
    assert_eq!(overwrite(std::hint::black_box(true)), 29);
    assert_eq!(overwrite(std::hint::black_box(false)), 43);
    assert_eq!(borrowed(std::hint::black_box(true)), 47);
    assert_eq!(borrowed(std::hint::black_box(false)), 61);
}
