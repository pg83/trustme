// `<<O as OutputMode>::Output as Mode>::Output<T>` over a type parameter `O`
// normalizes through no impl: upstream keeps it rigid - a trait bound does
// not normalize anything - whatever its generic associated type's arguments,
// relates two such aliases structurally, and so infers `T` from the
// declared `Output<usize>`. nom's `OM::Output::bind(|| count)`.
pub trait Mode {
    type Output<T>;
    fn bind<T, F: FnOnce() -> T>(f: F) -> Self::Output<T>;
}
pub struct Emit;
impl Mode for Emit {
    type Output<T> = T;
    fn bind<T, F: FnOnce() -> T>(f: F) -> T { f() }
}
pub trait OutputMode {
    type Output: Mode;
}
pub struct OM;
impl OutputMode for OM {
    type Output = Emit;
}
fn a<M: Mode>() -> M::Output<u8> { M::bind(|| 5u8) }
fn b<O: OutputMode>() -> <O::Output as Mode>::Output<u8> { O::Output::bind(|| 5u8) }
fn c<O: OutputMode>() -> <O::Output as Mode>::Output<u8> { <O::Output as Mode>::bind(|| 5u8) }
fn d<O: OutputMode>(n: u8) -> Result<(u8, <O::Output as Mode>::Output<usize>), ()> {
    let mut count = 0;
    loop {
        if n == 0 {
            return Ok((n, O::Output::bind(|| count)));
        }
        count += 1;
        if count > 3 {
            return Err(());
        }
    }
}
fn main() {
    assert_eq!(d::<OM>(0).unwrap().1, 0usize);
    assert_eq!(a::<Emit>(), 5);
    assert_eq!(b::<OM>(), 5);
    assert_eq!(c::<OM>(), 5);
}
