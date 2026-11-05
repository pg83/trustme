fn main() {
    assert_eq!(stringify!('λ'), "'λ'");
    assert_eq!(stringify!('\''), "'\\''");
    assert_eq!(stringify!(1.25f32), "1.25f32");
    assert_eq!(stringify!(2.5f64), "2.5f64");
    assert_eq!(stringify!(c"branch"), "c\"branch\"");
    assert_eq!(stringify!(cr#"a"b"#), "cr#\"a\"b\"#");
    let range_tokens = stringify!(1.25..=2.5);
    assert!(range_tokens.contains("1.25"));
    assert!(range_tokens.contains("..="));
    assert!(range_tokens.contains("2.5"));
}
