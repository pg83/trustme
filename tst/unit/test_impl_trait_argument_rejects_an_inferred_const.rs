//@ compile-fail: error
#![feature(generic_arg_infer)]

trait Tagged<const N: usize> {
    fn value() -> usize;
}

struct Entry;

impl Tagged<_> for Entry {
    fn value() -> usize {
        3
    }
}

fn main() {
    assert_eq!(<Entry as Tagged<3>>::value(), 3);
}
