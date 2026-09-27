// quote's test_interpolated_literal passes `-true` to a `$literal:literal`
// matcher. rustc's `parse_literal_maybe_minus` takes an optional `-` and then
// any literal token (`parse_token_lit`: numbers, strings, chars, byte and C
// strings, `true`/`false`); our matcher accepted only an integer or a float
// after the `-`, so no arm matched.
macro_rules! which {
    ($l:literal) => {
        1
    };
    ($($t:tt)*) => {
        2
    };
}

fn main() {
    assert_eq!(which!(-1), 1);
    assert_eq!(which!(-1.5), 1);
    assert_eq!(which!(true), 1);
    assert_eq!(which!(-true), 1);
    assert_eq!(which!(-false), 1);
    assert_eq!(which!(-"s"), 1);
    assert_eq!(which!(-b"s"), 1);
    assert_eq!(which!('c'), 1);
    assert_eq!(which!(-'c'), 1);
    assert_eq!(which!(-b'c'), 1);
}
