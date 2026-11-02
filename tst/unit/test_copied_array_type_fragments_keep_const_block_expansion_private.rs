//@ proc-macro-aux-build: retain_array_const_tokens.rs
//@ compile-variant: -O
macro_rules! duplicate {
    ($ty:ty) => {
        type First = $ty;
        #[retain_array_const_tokens::retain]
        fn consume(_: $ty) {}
    };
}
duplicate!([u8; {
    #[derive(Clone, PartialEq)]
    struct Local(bool);
    3
}]);
fn main() {
    let value: First = [17, 23, 29];
    consume(value);
    assert_eq!(std::mem::size_of::<First>(), 3);
}
