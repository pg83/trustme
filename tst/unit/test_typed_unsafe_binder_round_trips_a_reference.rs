#![feature(unsafe_binders, builtin_syntax)]

fn main() {
    let value = 47u16;
    unsafe {
        let bound = builtin #wrap_binder(&value, unsafe<'a> &'a u16);
        let reference = builtin #unwrap_binder(bound, unsafe<'a> &'a u16);
        assert_eq!(*reference, 47);
    }
}
