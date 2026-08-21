#![feature(fn_align, rustc_attrs)]

trait Aligned {
    #[rustc_align(4096)]
    fn call(&self);
}

fn main() {
    let address = <dyn Aligned>::call as fn(_) as usize;
    assert_eq!((address & !1) % 4096, 0);
}
