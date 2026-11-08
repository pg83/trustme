//@ compile-fail: error
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn width<const N: usize>()
where
    [(); const { let _callback = || { let value = N; value }; N }]:,
{}

fn main() {
    width::<3>();
}
