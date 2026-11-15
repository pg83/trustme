#![feature(decl_macro)]

enum Signal {
    Quiet,
    Ready(u8),
}

macro imported_value() {
    {
        use crate::Signal::Ready;
        match Ready(7) {
            crate::Signal::Quiet => 0,
            crate::Signal::Ready(value) => value,
        }
    }
}

fn main() {
    assert_eq!(imported_value!(), 7);
}
