//@ compile-variant: -O

#[derive(Clone, Copy)]
struct Pair { signed: i16, byte: u8 }
#[inline(never)]
fn repeated<T: Copy, const N: usize>(value: T) -> [T; N] { [value; N] }
#[inline(never)]
fn update(values: &mut [Pair; 3], index: usize) -> i16 {
    let slot = &mut values[index];
    slot.signed = -slot.signed + slot.byte as i16;
    slot.byte = slot.byte.wrapping_add(3);
    slot.signed
}
fn main() {
    let mut values = repeated::<_, 3>(Pair { signed: 11, byte: 5 });
    assert_eq!(update(&mut values, 1), -6);
    assert_eq!(values[0].signed, 11);
    assert_eq!(values[1].byte, 8);
    assert_eq!(values[2].byte, 5);
    assert_eq!(update(&mut values, 1), 14);
}
