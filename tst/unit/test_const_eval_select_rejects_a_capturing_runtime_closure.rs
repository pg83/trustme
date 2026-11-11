//@ compile-fail: const_eval_select runtime callback
#![feature(core_intrinsics, const_eval_select)]
#![allow(internal_features)]

const fn compile(value: u16) -> u16 {
    value + 11
}

fn main() {
    let increment = std::hint::black_box(11u16);
    let _ = std::intrinsics::const_eval_select(
        (7u16,),
        compile,
        move |value: u16| value + increment,
    );
}
