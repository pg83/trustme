#![feature(core_intrinsics, explicit_tail_calls)]
#![allow(incomplete_features, internal_features)]

#[inline(never)]
fn reverse(_: bool, value: u32) -> u32 { core::intrinsics::bitreverse(value) }

#[inline(never)]
fn count(_: bool, value: u32) -> u32 { core::intrinsics::ctpop(value) }

#[inline(never)]
fn route(flag: bool, value: u32) -> u32 {
    if flag { become reverse(flag, value) } else { become count(flag, value) }
}

fn main() {
    assert_eq!(route(true, 0x8000_0000), 1);
    assert_eq!(route(false, 0b1110_1011), 6);
}
