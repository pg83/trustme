//@ crate-type: lib
//@ proc-macro-aux-build: cov99_w561_empty_assoc_transport.rs

cov99_w561_empty_assoc_transport::empty_assoc! {
    pub trait Source {
        type EmptyName;
    }

    impl Source for () {
        type EmptyName = u8;
    }

    pub trait Gate {
        fn inspect() where Self: Source<EmptyName = u8> {}
    }

    impl Gate for () {}
}
