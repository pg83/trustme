//@ edition: 2021
//@ compile-fail: visibilit

mod outer {
    mod inner {
        pub(in outer) fn value() -> u16 { 107 }
    }
}

fn main() {}
