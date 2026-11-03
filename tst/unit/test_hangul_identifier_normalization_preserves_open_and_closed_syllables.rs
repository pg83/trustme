#![allow(non_snake_case, uncommon_codepoints)]
fn 가(value: u32) -> u32 { value + 449 }
fn 각(value: u32) -> u32 { value + 457 }
fn main() {
    let 한 = 461u32;
    let 한 = 한 + 2;
    assert_eq!(한, 463);
    assert_eq!(가(3), 452);
    assert_eq!(각(5), 462);
    assert_eq!(가(7) + 각(11), 924);
}
