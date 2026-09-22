// time-macros' `to_tokens!`: `$field_ty:ty = $default:pat` with the default
// `true`. The matcher's pattern scanner accepted string, byte-string, integer
// and float literals but not `true`, `false` or a char, so no arm matched. Upstream's pattern grammar begins a pattern with any literal,
// optionally negated (`parse_literal_maybe_minus`).
macro_rules! defaults {
    ($($name:ident : $ty:ty = $default:pat),* $(,)?) => {
        $( fn $name(x: $ty) -> bool { matches!(x, $default) } )*
    };
}
defaults! {
    is_true: bool = true,
    is_false: bool = false,
    is_a: char = 'a',
    is_lower: char = 'a'..='z',
    is_neg: i32 = -1,
}
fn main() {
    assert!(is_true(true) && !is_true(false));
    assert!(is_false(false) && !is_false(true));
    assert!(is_a('a') && !is_a('b'));
    assert!(is_lower('q') && !is_lower('Q'));
    assert!(is_neg(-1) && !is_neg(1));
}
