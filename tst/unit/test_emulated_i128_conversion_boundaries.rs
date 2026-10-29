//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
//@ output-contains: .cpp #define TRUSTME_TARGET_EMULATED_I128 1

use std::hint::black_box;

#[repr(i16)]
enum SignedTag { Below = -32761, Above = 30003 }
#[repr(u64)]
enum OneTag { Large = 0xffff_ffff_0000_0021 }
enum Singleton { Only = -32761 }

fn main() {
    let zero = black_box(0.0f64);
    let nan = zero / zero;
    let infinity = black_box(f64::INFINITY);
    let negative = black_box(f64::NEG_INFINITY);
    assert!(nan as i128 == 0 && nan as u128 == 0);
    assert!(infinity as i128 == i128::MAX && infinity as u128 == u128::MAX);
    assert!(negative as i128 == i128::MIN && negative as u128 == 0);
    assert!(black_box(-1.25f64) as u128 == 0);
    assert!(black_box(-0.0f32) as i128 == 0);
    assert!(black_box(65539.75f32) as i128 == 65539);
    assert!(black_box(65539.75f32) as u128 == 65539);
    assert!(black_box(-65539.75f64) as i128 == -65539);
    let bits = black_box(0x1000_0000_0000_0000_0000_0000_0000_00a5u128);
    assert!(bits as u8 == 0xa5 && bits as i8 == -91);
    assert!(bits as u16 == 0xa5 && bits as i16 == 0xa5);
    assert!(bits as u32 == 0xa5 && bits as i32 == 0xa5);
    assert!(bits as u64 == 0xa5 && bits as i64 == 0xa5);
    assert!(bits as usize == 0xa5 && bits as isize == 0xa5);
    assert!(black_box(-91i8) as i128 == -91);
    assert!(black_box(-30003i16) as i128 == -30003);
    assert!(black_box(0xffff_ffff_0000_0021u64) as u128 == 18446744069414584353);
    assert!(black_box(SignedTag::Below) as i128 == -32761);
    assert!(black_box(SignedTag::Above) as u128 == 30003);
    assert!(black_box(OneTag::Large) as u128 == 18446744069414584353);
    assert!(black_box(Singleton::Only) as i128 == -32761);
    assert!(black_box(Singleton::Only) as u128 == u128::MAX - 32760);
    let power = black_box(1u128 << 120);
    assert!(power as f32 == 1329227995784915872903807060280344576.0);
    assert!(power as f64 == 1329227995784915872903807060280344576.0);
    let signed = black_box(-(1i128 << 120));
    assert!(signed as f32 == -1329227995784915872903807060280344576.0);
    assert!(signed as f64 == -1329227995784915872903807060280344576.0);
    assert!((signed as u128) as i128 == signed);
}
