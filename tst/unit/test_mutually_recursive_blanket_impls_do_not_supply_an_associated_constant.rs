//@ compile-fail: error
trait North { const MARK: usize; }
trait South {}
impl<T: South> North for T { const MARK: usize = 17; }
impl<T: North> South for T {}
fn main() {
    let _ = <u8 as North>::MARK;
}
