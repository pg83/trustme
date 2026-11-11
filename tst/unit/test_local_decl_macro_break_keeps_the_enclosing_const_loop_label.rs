//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only
#![feature(decl_macro)]

pub const fn leave_once() -> u16 {
    'outer: loop {
        macro leave() {
            break 'outer 73u16;
        }
        leave!();
    }
}

pub const VALUE: u16 = leave_once();
const _: [(); 73] = [(); VALUE as usize];
