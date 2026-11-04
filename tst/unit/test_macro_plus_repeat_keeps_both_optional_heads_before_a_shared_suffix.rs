//@ compile-variant: -O
macro_rules! total {
    ($(tag $(left)? right $value:literal)+ tag end) => {{
        let mut result = 0u32;
        $(result += $value;)+
        result
    }};
}

fn main() {
    assert_eq!(total!(tag right 13 tag end), 13);
    assert_eq!(total!(tag left right 17 tag right 19 tag end), 36);
}
