//@ proc-macro-aux-build: echo_literal_tokens.rs

macro_rules! values {
    () => {
        (
            echo_literal_tokens::echo!(b"row\n\0\xfe"),
            stringify!(br#"raw "quote" \ tail"#),
            stringify!(r#"text "quoted" \ tail"#),
        )
    };
}

fn main() {
    let (bytes, byte_text, text) = values!();
    assert_eq!(bytes, b"row\n\0\xfe");
    assert_eq!(byte_text, r##"br#"raw "quote" \ tail"#"##);
    assert_eq!(text, r##"r#"text "quoted" \ tail"#"##);
}
