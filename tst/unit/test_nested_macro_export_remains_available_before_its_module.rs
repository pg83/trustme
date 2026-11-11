fn main() {
    assert_eq!(crate::exported_code!(), 23u16);
}

mod owner {
    #[macro_export]
    macro_rules! exported_code {
        () => { 23u16 };
    }
}
