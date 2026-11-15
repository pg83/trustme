//@ compile-flags: -Zdump-hir -Zdump-mir

static VALUE: u32 = 37;

fn shorten<'a>(value: &'static u32) -> &'a u32 {
    let result: &'a u32 = value else {
        unreachable!();
    };
    result
}

fn main() {
    assert_eq!(*shorten(&VALUE), 37);
}
