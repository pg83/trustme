//@ compile-variant: -O
#[inline(never)]
fn tagged<const MARK: char>(extra: u32) -> (char, u32) {
    (MARK, MARK as u32 + extra)
}
fn main() {
    assert_eq!(tagged::<'λ'>(3), ('λ', 958));
    assert_eq!(tagged::<'𐐀'>(5), ('𐐀', 66565));
    let call: fn(u32) -> (char, u32) = tagged::<'🦀'>;
    assert_eq!(call(7), ('🦀', 129415));
}
