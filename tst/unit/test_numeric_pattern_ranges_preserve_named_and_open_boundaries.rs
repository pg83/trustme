macro_rules! check {
    ($ty:ty) => {{
        fn select(value: $ty) -> u8 {
            const LOW: $ty = 17 as $ty;
            const HIGH: $ty = 23 as $ty;
            match value { ..LOW => 1, LOW..=HIGH => 2, HIGH.. => 3, _ => 4 }
        }
        assert_eq!(select(16 as $ty), 1);
        assert_eq!(select(17 as $ty), 2);
        assert_eq!(select(23 as $ty), 2);
        assert_eq!(select(24 as $ty), 3);
    }};
}
fn main() {
    check!(u8); check!(u16); check!(u32); check!(u64); check!(u128); check!(usize);
    check!(i8); check!(i16); check!(i32); check!(i64); check!(i128); check!(isize);
    check!(f32); check!(f64);
    let text = 'm';
    assert_eq!(match text { ..'a' => 1, 'a'..='m' => 2, 'n'.. => 3 }, 2);
    assert_eq!(match 37u16 { 0..17 => 1, 17..=37 => 2, _ => 3 }, 2);
    assert_eq!(match -19i64 { -23..=-17 => 1, _ => 0 }, 1);
    assert_eq!(match 1.75f64 { 1.25..2.5 => 1, _ => 0 }, 1);
    assert_eq!(match "rust" { "rust" => 1, _ => 0 }, 1);
}
