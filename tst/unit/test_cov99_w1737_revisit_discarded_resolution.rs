#![allow(unused_imports)]

pub(crate) mod first {
    pub(crate) use crate::second::*;
    pub(crate) use crate::origin::*;
}

pub(crate) mod second {
    pub(crate) use crate::first::*;
}

pub(crate) mod origin {
    pub(crate) mod hop {
        pub(crate) use crate::second as retry;

        macro_rules! finish {
            () => { 73 };
        }
        pub(crate) use finish;
    }
}

fn main() {
    assert_eq!(crate::first::hop::retry::hop::finish!(), 73);
}
