//@ compile-fail: error
#![feature(generic_arg_infer)]

trait First {
    fn first(&self) -> u8;
}

impl First for [u8; _] {
    fn first(&self) -> u8 {
        self[0]
    }
}

fn main() {
    assert_eq!(<[u8; 3] as First>::first(&[7, 9, 11]), 7);
}
