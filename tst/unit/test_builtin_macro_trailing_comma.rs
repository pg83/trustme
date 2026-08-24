mod nested_inline_module {
    pub fn check_relative_includes() {
        let source = include_str!("test_builtin_macro_trailing_comma.rs");
        assert!(source.contains("nested_inline_module"));

        let bytes = include_bytes!("test_builtin_macro_trailing_comma.rs");
        assert!(!bytes.is_empty());
    }
}

fn main() {
    assert!(!env!("PATH",).is_empty());
    assert_eq!(option_env!("TRUSTME_MISSING_TEST_VARIABLE",), None);

    let source = include_str!("test_builtin_macro_trailing_comma.rs",);
    assert!(source.contains("include_str!"));

    let bytes = include_bytes!("test_builtin_macro_trailing_comma.rs",);
    assert!(!bytes.is_empty());

    nested_inline_module::check_relative_includes();
}
