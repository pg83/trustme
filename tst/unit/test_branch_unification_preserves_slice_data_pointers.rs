//@ compile-variant: -O
#[inline(never)]
fn same(flag: bool, a: &[u8]) -> *const u8 {
    if flag { a as *const [u8] as *const u8 } else { a as *const [u8] as *const u8 }
}

#[inline(never)]
fn different(flag: bool, a: &[u8], b: &[u8]) -> *const u8 {
    if flag { a as *const [u8] as *const u8 } else { b as *const [u8] as *const u8 }
}

fn main() {
    let bytes = [13u8, 29, 41, 67, 89];
    let a = &bytes[..2];
    let b = &bytes[2..];
    for flag in [false, true] {
        assert_eq!(same(flag, a), a.as_ptr());
        assert_eq!(different(flag, a, b), if flag { a.as_ptr() } else { b.as_ptr() });
    }
}
