// Extracted from library/std/src/keyword_docs.rs:364
#![allow(unused)]
fn main() {
    struct Coord;
    enum SimpleEnum {
        FirstVariant,
        SecondVariant,
        ThirdVariant,
    }

    enum Location {
        Unknown,
        Anonymous,
        Known(Coord),
    }

    enum ComplexEnum {
        Nothing,
        Something(u32),
        LotsOfThings {
            usual_struct_stuff: bool,
            blah: String,
        }
    }

    enum EmptyEnum { }
}
