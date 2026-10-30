trait Family { type Part; }
impl Family for () { type Part = u8; }
struct Wrap<T>(T);
trait Source { type Output: Copy; }
impl Source for Wrap<u8> { type Output = u16; }
impl Source for () { type Output = u8; }
fn duplicate<T: Family<Part = u8>, U: Source>(value: U::Output) -> (U::Output, U::Output)
where Wrap<T::Part>: Source {
    (value, value)
}
fn main() { assert_eq!(duplicate::<(), ()>(29), (29, 29)); }
