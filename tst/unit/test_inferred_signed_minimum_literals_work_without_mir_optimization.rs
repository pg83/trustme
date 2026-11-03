//@ compile-flags: -Zmir-opt-level=0 -Coverflow-checks=yes

const LOWEST: i128 = -170141183460469231731687303715884105728;

#[inline(never)] fn byte() -> i8 { -128 }
#[inline(never)] fn word() -> i16 { -32768 }
#[inline(never)] fn double_word() -> i32 { -2147483648 }
#[inline(never)] fn quad_word() -> i64 { -9223372036854775808 }
#[inline(never)] fn wide() -> i128 { -170141183460469231731687303715884105728 }
#[inline(never)] fn address() -> isize { -9223372036854775808 }

fn main() {
    assert_eq!([byte(), -128i8], [i8::MIN; 2]);
    assert_eq!([word(), -32768], [i16::MIN; 2]);
    assert_eq!([double_word(), -2147483648], [i32::MIN; 2]);
    assert_eq!([quad_word(), -9223372036854775808], [i64::MIN; 2]);
    assert_eq!([wide(), LOWEST, -0x80000000000000000000000000000000], [i128::MIN; 3]);
    assert_eq!(address(), isize::MIN);
    let default_type = -2147483648;
    assert_eq!(default_type, i32::MIN);
    assert_eq!(-(-5), 5);
    assert_eq!(-3.5f64, -3.5);
    assert_eq!(!17u8, 238);
}
