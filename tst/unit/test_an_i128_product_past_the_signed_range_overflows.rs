use std::hint::black_box;

fn main() {
    let third: i128 = black_box(i128::MAX / 3);
    assert_eq!(third.checked_mul(black_box(4)), None);
    assert_eq!(third.overflowing_mul(black_box(4)), (third.wrapping_mul(4), true));
    assert_eq!(black_box(-third).checked_mul(black_box(4)), None);
    assert_eq!(black_box(i128::MIN).checked_mul(black_box(-1)), None);
    assert_eq!(black_box(1i128 << 126).checked_mul(black_box(-2)), Some(i128::MIN));
    assert_eq!(black_box(1i128 << 126).checked_mul(black_box(2)), None);
    assert_eq!(black_box(-3i128).checked_mul(black_box(-5)), Some(15));
    assert_eq!("340282366920938463463374607431768211455".parse::<i128>().is_err(), true);
}
