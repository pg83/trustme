//@ compile-fail: Mismatched return type:

type Twice<T> = (T, T);
trait Repeat { fn repeat() -> Twice<impl Copy>; }
struct Source;
impl Repeat for Source {
    fn repeat() -> (impl Copy, impl Copy) { (1u8, 2u8) }
}
fn main() { let _ = Source::repeat(); }
