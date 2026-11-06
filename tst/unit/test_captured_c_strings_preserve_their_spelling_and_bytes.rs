macro_rules! source_text {
    ($value:expr) => {
        stringify!($value)
    };
}

fn main() {
    assert_eq!(source_text!(c"line\nend"), "c\"line\\nend\"");
    assert_eq!(source_text!(cr#"a"b\c"#), "cr#\"a\"b\\c\"#");
    assert!(c"line\nend".to_bytes() == [108, 105, 110, 101, 10, 101, 110, 100]);
    assert!(cr#"a"b\c"#.to_bytes() == [97, 34, 98, 92, 99]);
}
