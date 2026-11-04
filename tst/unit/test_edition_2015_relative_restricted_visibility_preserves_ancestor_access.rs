//@ edition: 2015
//@ compile-variant: -O

mod outer {
    mod inner {
        pub(in outer) fn value() -> u16 { 67 }
    }
    pub fn value() -> u16 { inner::value() + 4 }
}

fn main() { assert_eq!(outer::value(), 71); }
