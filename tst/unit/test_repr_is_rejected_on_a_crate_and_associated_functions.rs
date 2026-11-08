//@ compile-fail:error
#![repr(C)]

struct Word;
impl Word {
    #[repr(C)]
    fn read() {}
}

trait Read {
    #[repr(C)]
    fn read();
}

fn main() {}
