//@ compile-fail: Extra tokens after parsed type

macro_rules! malformed_type {
    () => { u16 u32 };
}

fn main() {
    let _: malformed_type!();
}
