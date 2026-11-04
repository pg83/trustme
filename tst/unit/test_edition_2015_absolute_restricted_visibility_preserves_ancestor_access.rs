//@ edition: 2015
//@ compile-variant: -O

mod outer {
    mod inner {
        pub(in ::outer) fn value() -> u16 { 73 }
    }
    pub fn value() -> u16 { inner::value() + 6 }
}

fn main() { assert_eq!(outer::value(), 79); }
