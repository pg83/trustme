//@ compile-variant: -O
#[inline(never)]
fn same(flag: bool, a: *const u8) -> usize {
    if flag { a as usize } else { a as usize }
}

#[inline(never)]
fn different(flag: bool, a: *const u8, b: *const u8) -> usize {
    if flag { a as usize } else { b as usize }
}

fn main() {
    let bytes = [23u8, 71];
    let a = &bytes[0] as *const u8;
    let b = &bytes[1] as *const u8;
    for flag in [false, true] {
        assert_eq!(same(flag, a), a as usize);
        assert_eq!(different(flag, a, b), if flag { a as usize } else { b as usize });
    }
}
