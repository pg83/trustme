//@ compile-variant: -O
#![feature(ptr_metadata)]

#[inline(never)]
fn same(flag: bool, a: *const [u16]) -> usize {
    if flag { core::ptr::metadata(a) } else { core::ptr::metadata(a) }
}

#[inline(never)]
fn different(flag: bool, a: *const [u16], b: *const [u16]) -> usize {
    if flag { core::ptr::metadata(a) } else { core::ptr::metadata(b) }
}

fn main() {
    let bytes = [7u16, 19, 31, 43, 59];
    let a = &bytes[..2] as *const [u16];
    let b = &bytes[2..] as *const [u16];
    for flag in [false, true] {
        assert_eq!(same(flag, a), 2);
        assert_eq!(different(flag, a, b), if flag { 2 } else { 3 });
    }
}
