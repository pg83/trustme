//@ test-harness

#[test]
fn root_name() {
    assert_eq!(std::thread::current().name(), Some("root_name"));
}

mod nested {
    #[test]
    fn nested_name() {
        assert_eq!(
            std::thread::current().name(),
            Some("nested::nested_name"),
        );
    }
}
