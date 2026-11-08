//@ compile-fail: Unknown literal prefix
#![feature(unsafe_binders, builtin_syntax)]

fn main() {
    let value = 43u16;
    unsafe {
        let bound = builtin#wrap_binder(&value, unsafe<'a> &'a u16);
        let reference = builtin#unwrap_binder(bound, &u16);
        assert_eq!(*reference, 43);
    }
}
