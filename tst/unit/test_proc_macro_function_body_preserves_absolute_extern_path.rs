//@ proc-macro-aux-build: use_item_identity.rs

macro_rules! through_tokens {
    ($function:item) => { use_item_identity::echo!($function); };
}

mod caller {
    mod std {
        pub mod mem {
            pub fn size_of<T>() -> usize {
                59
            }
        }
    }

    through_tokens! {
        fn bytes() -> usize {
            ::std::mem::size_of::<[u16; 3]>()
        }
    }

    pub fn check() {
        assert_eq!(bytes(), 6);
        assert_eq!(std::mem::size_of::<[u16; 3]>(), 59);
    }
}

fn main() {
    caller::check();
}
