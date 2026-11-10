pub use own_expression_attribute_identity::identity;

#[macro_export]
macro_rules! check {
    () => {
        assert!(#[$crate::identity] (59u16 == 59u16));
    };
}
