macro_rules! select {
    ($binding:ident, $pattern:pat, $value:expr, $body:expr) => {
        match $value {
            $binding @ $pattern => $body,
            _ => 0,
        }
    };
}

fn main() {
    assert_eq!(select!(whole, Some(inner), Some(307u16), whole.unwrap() + inner), 614);

    let value = match Some((3u16, 4u16)) {
        Some((2..=5, value)) => value + 5,
        _ => 0,
    };
    assert_eq!(value, 9);
}
