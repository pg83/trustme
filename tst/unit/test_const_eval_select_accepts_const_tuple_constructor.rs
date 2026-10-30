#![feature(core_intrinsics, const_eval_select)]
#![allow(internal_features)]

struct Wrap(u32);

const VALUE: Wrap = std::intrinsics::const_eval_select((17,), Wrap, Wrap);

fn main() {
    assert_eq!(VALUE.0, 17);
}
