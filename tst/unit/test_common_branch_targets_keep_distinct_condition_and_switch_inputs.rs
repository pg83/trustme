//@ compile-flags: -O

#[inline(never)]
fn condition(side: bool, left: bool, right: bool) -> u16 {
    if side {
        if left { 17 } else { 19 }
    } else {
        if right { 17 } else { 19 }
    }
}

#[inline(never)]
fn switch(side: bool, left: u8, right: u8) -> u16 {
    if side {
        match left { 0 => 23, 1 => 29, _ => 31 }
    } else {
        match right { 0 => 23, 1 => 29, _ => 31 }
    }
}

fn main() {
    for left in [false, true] {
        for right in [false, true] {
            assert_eq!(condition(true, left, right), if left { 17 } else { 19 });
            assert_eq!(condition(false, left, right), if right { 17 } else { 19 });
        }
    }
    let expected = [23, 29, 31];
    for left in 0..3 {
        for right in 0..3 {
            assert_eq!(switch(true, left, right), expected[left as usize]);
            assert_eq!(switch(false, left, right), expected[right as usize]);
        }
    }
}
