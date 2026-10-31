macro_rules! check {
    ($name:ident, $ty:ty, $value:expr) => {
        fn $name(value: $ty) -> bool {
            const PATTERN: $ty = $value;
            match value { PATTERN => true, _ => false }
        }
    };
}
check!(signed_byte, i8, -17);
check!(signed_short, i16, -257);
check!(signed_long, i64, -4294967333);
check!(wide_unsigned, u128, (1u128 << 100) | 37);
check!(boolean, bool, true);
check!(character, char, '\u{1f980}');
const TUPLE: (i8, u128, bool, char) = (-17, (1u128 << 100) | 37, true, '\u{1f980}');
fn tuple(value: (i8, u128, bool, char)) -> bool { match value { TUPLE => true, _ => false } }
fn main() {
    assert!(signed_byte(-17) && !signed_byte(-16));
    assert!(signed_short(-257) && !signed_short(257));
    assert!(signed_long(-4294967333) && !signed_long(37));
    assert!(wide_unsigned((1u128 << 100) | 37) && !wide_unsigned(37));
    assert!(boolean(true) && !boolean(false));
    assert!(character('\u{1f980}') && !character('q'));
    assert!(tuple(TUPLE) && !tuple((-17, 37, true, '\u{1f980}')));
}
