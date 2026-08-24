//@ test-harness

mod api {
    pub enum ConvertError<A, S, V> {
        Alignment(A),
        Size(S),
        Validity(V),
    }

    pub type ReadError<S, V> = ConvertError<(), S, V>;

    impl<S, V> ReadError<S, V> {
        pub fn is_size(self) -> bool {
            match self {
                Self::Size(_) => true,
                _ => false,
            }
        }
    }
}

mod tests {
    use super::api::*;

    #[test]
    fn generic_type_alias_enum_pattern() {
        assert!(ConvertError::<(), u8, u16>::Size(42).is_size());
    }
}
