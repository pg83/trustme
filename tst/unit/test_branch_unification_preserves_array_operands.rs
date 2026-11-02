//@ compile-variant: -O
#[inline(never)]
fn same(flag: bool, a: u16, b: u16) -> [u16; 3] {
    if flag { [a, b, a] } else { [a, b, a] }
}

#[inline(never)]
fn different(flag: bool, a: u16, b: u16) -> [u16; 3] {
    if flag { [a, b, a] } else { [a, b, b] }
}

fn main() {
    for flag in [false, true] {
        assert_eq!(same(flag, 17, 53), [17, 53, 17]);
        assert_eq!(different(flag, 17, 53), [17, 53, if flag { 17 } else { 53 }]);
    }
}
