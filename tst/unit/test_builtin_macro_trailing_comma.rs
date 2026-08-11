fn main() {
    assert!(!env!("PATH",).is_empty());
    assert_eq!(option_env!("TRUSTME_MISSING_TEST_VARIABLE",), None);

    let source = include_str!("test_builtin_macro_trailing_comma.rs",);
    assert!(source.contains("include_str!"));

    let bytes = include_bytes!("test_builtin_macro_trailing_comma.rs",);
    assert!(!bytes.is_empty());
}
