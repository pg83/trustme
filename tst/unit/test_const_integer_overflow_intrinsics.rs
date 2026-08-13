const _: () = {
    let add = i16::MAX.overflowing_add(1);
    assert!(add.0 == i16::MIN && add.1);
    let sub = i16::MIN.overflowing_sub(1);
    assert!(sub.0 == i16::MAX && sub.1);
    let mul = (20_000i16).overflowing_mul(2);
    assert!(mul.0 == -25_536 && mul.1);

    let unsigned_mul = (100u8).overflowing_mul(4);
    assert!(unsigned_mul.0 == 144 && unsigned_mul.1);

    assert!(i16::MAX.saturating_add(1) == i16::MAX);
    assert!(i16::MIN.saturating_sub(1) == i16::MIN);

    let add128 = i128::MAX.overflowing_add(1);
    assert!(add128.0 == i128::MIN && add128.1);
    let sub128 = i128::MIN.overflowing_sub(1);
    assert!(sub128.0 == i128::MAX && sub128.1);
    let mul128 = i128::MAX.overflowing_mul(2);
    assert!(mul128.0 == -2 && mul128.1);

    assert!(23i16 % -8 == 7);
    assert!(-23i16 % 8 == -7);

    assert!((-15i16).signum() == -1);
    assert!(0i16.signum() == 0);
    assert!(15i16.signum() == 1);
    assert!(i128::MIN.signum() == -1);

    assert!(i128::MIN >> 127 == -1);
    assert!(i128::MIN.unbounded_shr(128) == -1);
    assert!((-1i128).unbounded_shr(65) == -1);
};

fn main() {}
