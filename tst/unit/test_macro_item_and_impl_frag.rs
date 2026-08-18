// An `$item:item` fragment stands in an extern block, and `impl $ty` is not a
// type -- so an arm asking for a type does not match it and the next arm does.
macro_rules! mac_extern {
    ($i:item) => {
        extern "C" {
            $i
        }
    };
}

mac_extern! {
    fn strlen(s: *const u8) -> usize;
}

macro_rules! impl_primitive {
    ($ty:ty) => {
        impl_primitive!(impl $ty);
    };
    (impl $ty:ty) => {
        fn takes(_: $ty) -> u32 { 7 }
    };
}

impl_primitive! { u8 }

macro_rules! pick {
    ($ty:ty) => { 0 };
    (impl &) => { 1 };
}

fn main() {
    assert_eq!(takes(1u8), 7);
    assert_eq!(pick!(u8), 0);
    assert_eq!(pick!(impl &), 1);
    let _ = strlen;
}
