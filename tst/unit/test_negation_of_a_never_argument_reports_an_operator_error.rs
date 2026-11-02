//@ compile-fail: cannot apply unary operator `-` to type `!`
#![feature(never_type)]

fn invert(value: !) -> u32 { !value }
fn negate(value: !) -> i32 { -value }

fn main() {
    let _: fn(!) -> u32 = invert;
    let _: fn(!) -> i32 = negate;
}
