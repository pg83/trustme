//@ compile-fail:error
struct Word;

impl Word {
    #[repr(C)]
    fn read() {}
}

fn main() {}
