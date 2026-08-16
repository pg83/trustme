// A trait item is public by definition, but the grammar still accepts a
// visibility there — and an item stripped by `cfg` never reaches the check
// that rejects it, so it has to parse.
//
// Same shape as the gccrs test trait_pub_type.rs.
#[cfg(FALSE)]
trait Stripped {
    pub type X;
    pub fn f();
    pub const C: u8;
}

trait Real {
    fn f(&self) -> u8;
}

impl Real for () {
    fn f(&self) -> u8 {
        3
    }
}

fn main() {
    assert_eq!(().f(), 3);
}
