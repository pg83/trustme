//@ compile-variant: -O
#[inline(never)]
fn select(first: bool, left: bool, right: bool) -> u16 {
    if first {
        if left { 13 } else { 17 }
    } else {
        if right { 13 } else { 17 }
    }
}
fn main() {
    for left in [false, true] {
        for right in [false, true] {
            assert_eq!(select(true, left, right), if left { 13 } else { 17 });
            assert_eq!(select(false, left, right), if right { 13 } else { 17 });
        }
    }
}
