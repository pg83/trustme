// Extracted from library/core/src/primitive.rs:24
#![allow(unused)]
fn main() {
    #[allow(non_camel_case_types)]
    pub struct bool;

    impl QueryId for bool {
        const SOME_PROPERTY: ::core::primitive::bool = true;
    }

    trait QueryId { const SOME_PROPERTY: ::core::primitive::bool; }
}
