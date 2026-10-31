macro_rules! evaluate { ($body:expr) => {{ $body; 11u8 }}; }
fn main() {
    macro_rules! add { ($target:ident, $value:expr) => { $target += $value }; }
    let mut total = 0;
    let result = evaluate!({ for item in [1u8, 2, 3] { add!(total, item); } });
    assert_eq!((total, result), (6, 11));
}
