#![feature(intrinsics)]
#![allow(internal_features)]

extern "rust-intrinsic" {
    fn type_name<T: ?Sized>() -> &'static str;
}

fn main() {
    assert_eq!(unsafe { type_name::<u16>() }, "u16");
    assert_eq!(unsafe { type_name::<str>() }, "str");
    assert_eq!(unsafe { type_name::<[u16]>() }, "[u16]");
    assert_eq!(
        unsafe { type_name::<(u8, i32)>() },
        std::any::type_name::<(u8, i32)>(),
    );
    assert_eq!(
        unsafe { type_name::<fn(&u8) -> usize>() },
        std::any::type_name::<fn(&u8) -> usize>(),
    );
}
