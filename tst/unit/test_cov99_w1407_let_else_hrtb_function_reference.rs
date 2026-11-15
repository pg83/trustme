//@ compile-flags: -Zdump-hir -Zdump-mir

static VALUE: u32 = 41;

fn read(value: &u32) -> u32 {
    *value
}

fn invoke(input: &for<'a> fn(&'a u32) -> u32) -> u32 {
    let function: &fn(&'static u32) -> u32 = input else {
        unreachable!();
    };
    function(&VALUE)
}

fn main() {
    let function: fn(&u32) -> u32 = read;
    assert_eq!(invoke(&function), 41);
}
