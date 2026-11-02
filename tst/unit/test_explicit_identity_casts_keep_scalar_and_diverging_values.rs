//@ compile-variant: -O
#[inline(never)]
fn choose(flag: bool) -> u8 {
    if flag { (return 3) as u8 } else { 7u8 as u8 }
}

fn main() {
    assert_eq!(true as bool, true);
    assert_eq!('Q' as char, 'Q');
    assert_eq!(19i32 as i32, 19);
    assert_eq!(choose(true), 3);
    assert_eq!(choose(false), 7);
}
