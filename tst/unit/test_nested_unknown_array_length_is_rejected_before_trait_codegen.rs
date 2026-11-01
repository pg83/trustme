//@ compile-fail: Failed to infer const argument
#![feature(generic_arg_infer)]

trait Width { fn width() -> usize; }
impl<const N: usize> Width for [[u16; N]; 2] {
    fn width() -> usize { N * 2 }
}

fn main() {
    let _ = <[[u16; _]; 2] as Width>::width();
}
