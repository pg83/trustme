macro_rules! local {
    () => { 7u16 };
}

macro_rules! local {
    () => { 11u16 };
}

mod catalog {
    #[macro_export]
    macro_rules! first {
        () => { 13u16 };
    }

    #[macro_export]
    macro_rules! second {
        () => { 17u16 };
    }
}

mod left {
    pub use crate::first;
}

mod right {
    pub use crate::first;
}

fn main() {
    assert_eq!(local!(), 11);
    assert_eq!(left::first!(), 13);
    assert_eq!(right::first!(), 13);
    assert_eq!(crate::first!() + crate::second!(), 30);
}
