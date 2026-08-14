#![feature(fn_delegation)]
#![allow(incomplete_features)]

mod nested {
    pub trait Selected {
        fn value(&self) -> u8 { 1 }
    }

    pub struct Inner;

    impl Selected for Inner {}

    impl Inner {
        pub fn value(&self) -> u8 { 2 }
    }

    pub struct Outer(pub Inner);

    impl Selected for Outer {
        reuse Selected::value { self.0 }
    }
}

fn main() {
    use nested::Selected;
    assert_eq!(nested::Outer(nested::Inner).value(), 1);
}
