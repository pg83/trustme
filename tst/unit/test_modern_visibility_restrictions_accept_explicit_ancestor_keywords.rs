//@ compile-variant: -O

mod outer {
    mod inner {
        pub(in crate::outer) fn rooted() -> u16 { local() }
        pub(in self) fn local() -> u16 { 149 }
        pub(in super) fn parent() -> u16 { 151 }
    }
    pub fn value() -> u16 { inner::rooted() + inner::parent() }
}

fn main() { assert_eq!(outer::value(), 300); }
