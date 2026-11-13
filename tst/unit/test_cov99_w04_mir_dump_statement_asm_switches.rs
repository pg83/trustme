//@ compile-flags: -Zdump-mir
//@ output-contains: _3_mir.rs asm2!(
//@ output-contains: _3_mir.rs const Int(v = 3, t = i32)
//@ output-contains: _3_mir.rs sym 

use std::arch::asm;

static MARKER: u8 = 9;

#[inline(never)]
fn add_three(mut value: u64) -> u64 {
    unsafe {
        asm!(
            "add {value}, {step}",
            "/* {symbol} */",
            value = inout(reg) value,
            step = const 3,
            symbol = sym MARKER,
            options(nomem, nostack),
        );
    }
    value
}

#[inline(never)]
fn through_pointer(function: fn(u64) -> u64, value: u64) -> u64 {
    function(value)
}

#[inline(never)]
fn unsigned(value: u8) -> u8 {
    match value { 1 | 4 => 8, 9 => 12, _ => 2 }
}

#[inline(never)]
fn signed(value: i16) -> i16 {
    match value { -7 => 3, 11 => 4, _ => 5 }
}

#[inline(never)]
fn text(value: &str) -> u8 {
    match value { "left" => 6, "right" => 7, _ => 0 }
}

#[inline(never)]
fn choose(value: bool) -> u8 {
    if value { unsigned(4) } else { unsigned(9) }
}

fn main() {
    assert_eq!(through_pointer(add_three, 5), 8);
    assert_eq!(choose(true), 8);
    assert_eq!(choose(false), 12);
    assert_eq!(signed(-7), 3);
    assert_eq!(signed(11), 4);
    assert_eq!(signed(0), 5);
    assert_eq!(text("left"), 6);
    assert_eq!(text("right"), 7);
    assert_eq!(text("other"), 0);
}
