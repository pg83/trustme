//@ compile-fail:error
trait Read {
    #[repr(C)]
    fn read();
}

fn main() {}
