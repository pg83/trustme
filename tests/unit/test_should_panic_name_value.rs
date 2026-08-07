//@ test-harness

mod regression {
    #[test]
    #[should_panic = "expected panic"]
    fn legacy_should_panic_message() {
        panic!("expected panic");
    }
}
