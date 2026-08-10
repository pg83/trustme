// Extracted from library/core/src/slice/mod.rs:4796
#![allow(unused)]
#![feature(substr_range)]
fn main() {

    let nums: &[u32] = &[1, 7, 1, 1];
    let num = &nums[2];

    assert_eq!(num, &1);
    assert_eq!(nums.element_offset(num), Some(2));
}
