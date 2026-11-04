//@ compile-variant: -O
macro_rules! total {
    ($(tag $(left)? right $value:literal)* tag end) => {{
        let mut result = 0u32;
        $(result += $value;)*
        result
    }};
}

fn main() {
    assert_eq!(total!(tag end), 0);
    assert_eq!(total!(tag right 3 tag end), 3);
    assert_eq!(total!(tag left right 5 tag right 7 tag left right 11 tag end), 23);
}
