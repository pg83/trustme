#[macro_use]
mod source {
    macro_rules! mix { ($value:expr) => { $value * 3 + 1 }; }
}
mod relay {
    pub(crate) use mix as first;
    mod inner {
        pub(crate) use super::first as second;
    }
    pub(crate) use inner::second as final_mix;
}
mod wildcard {
    pub(crate) use crate::relay::*;
}
macro_rules! make_user {
    ($name:ident) => {
        mod $name {
            use crate::wildcard::final_mix as apply;
            pub fn value(input: u32) -> u32 { apply!(input) }
        }
    };
}
make_user!(generated);
fn main() {
    assert_eq!(generated::value(7), 22);
    assert_eq!(relay::final_mix!(10), 31);
    assert_eq!(mix!(4), 13);
}
