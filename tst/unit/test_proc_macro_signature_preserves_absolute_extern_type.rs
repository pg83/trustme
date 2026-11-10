//@ proc-macro-aux-build: use_item_identity.rs

macro_rules! relay_signature {
    ($function:item) => { use_item_identity::echo!($function); };
}

mod caller {
    mod std {
        pub mod num {
            pub type NonZeroU8 = bool;
        }
    }

    relay_signature! {
        fn wrap(value: ::std::num::NonZeroU8) -> Option<::std::num::NonZeroU8> {
            Some(value)
        }
    }

    pub fn check() {
        let value = ::std::num::NonZeroU8::new(41).unwrap();
        assert_eq!(wrap(value).unwrap().get(), 41);
        let local: std::num::NonZeroU8 = false;
        assert!(!local);
    }
}

fn main() {
    caller::check();
}
