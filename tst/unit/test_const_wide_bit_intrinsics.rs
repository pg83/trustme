const _: () = {
    assert!(0x12u8.swap_bytes() == 0x12);
    assert!(0x1234u16.swap_bytes() == 0x3412);
    assert!(0x12345678u32.swap_bytes() == 0x78563412);
    assert!(0x0102030405060708u64.swap_bytes() == 0x0807060504030201);
    assert!(0x0102030405060708090a0b0c0d0e0f10u128.swap_bytes()
        == 0x100f0e0d0c0b0a090807060504030201);
    assert!((-128i8).swap_bytes() == -128);
    assert!((-256i16).swap_bytes() == 255);
    assert!((-16777216i32).swap_bytes() == 255);
    assert!(i64::MIN.swap_bytes() == 128);
    assert!(i128::MIN.swap_bytes() == 128);

    assert!(0x81u128.reverse_bits() == 0x81000000000000000000000000000000);
    assert!((-2i128).reverse_bits() == i128::MAX);
    assert!(0u128.leading_zeros() == 128);
    assert!(0u128.trailing_zeros() == 128);
    assert!((1u128 << 100).leading_zeros() == 27);
    assert!((1u128 << 100).trailing_zeros() == 100);
    assert!((u128::MAX ^ 0x81).count_ones() == 126);
    assert!(i128::MIN.count_ones() == 1);
    assert!(0x81u128.rotate_left(120) == 0x81000000000000000000000000000000);
    assert!(0x81u128.rotate_right(8) == 0x81000000000000000000000000000000);

    let high = 1u128 << 100;
    assert!((high + 35) / high == 1);
    assert!((high + 35) % high == 35);
    assert!((high + 35) / 7 == 181092942889747057356671886487);
    assert!((high + 35) % 7 == 2);
    assert!((-35i128) / 7 == -5);
    assert!((-35i128) % 8 == -3);
    assert!(u128::MAX.saturating_add(1) == u128::MAX);
    assert!(0u128.saturating_sub(1) == 0);
    assert!(i128::MIN.saturating_add(-1) == i128::MIN);
    assert!(i128::MAX.saturating_sub(-1) == i128::MAX);
};

fn main() {}
