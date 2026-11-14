//@ compile-only
//@ proc-macro-aux-build: cov99_w561_empty_assoc_transport.rs

cov99_w561_empty_assoc_transport::empty_assoc! {
    trait Source {
        type EmptyName;
    }

    impl Source for () {
        type EmptyName = u8;
    }

    trait Gate {
        fn inspect() where Self: Source<EmptyName = u8> {}
    }

    impl Gate for () {}
}

fn main() {}
