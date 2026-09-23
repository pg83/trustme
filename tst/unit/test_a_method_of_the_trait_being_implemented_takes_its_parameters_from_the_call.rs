// time's `impl Distribution<OffsetDateTime> for StandardUniform` calls
// `date_time.assume_offset(Self.sample(rng))`, where the argument is a
// `UtcOffset`. Upstream picks `<StandardUniform as Distribution<?T>>::sample`
// and coerces the argument, of type `?T`, into `UtcOffset`: a coercion from
// an inference variable into a known type unifies them
// (`coerce_from_inference_variable`). We left that coercion for the end,
// and the fallback that prefers the enclosing impl's trait arguments
// (`OffsetDateTime`) got to `?T` first.
pub trait Rng { fn next(&mut self) -> u32; }
struct Counter(u32);
impl Rng for Counter { fn next(&mut self) -> u32 { self.0 += 1; self.0 } }
pub trait Distribution<T> { fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> T; }
impl<'a, T, D: Distribution<T> + ?Sized> Distribution<T> for &'a D {
    fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> T { (**self).sample(rng) }
}
pub struct Standard;
#[derive(Debug, PartialEq)] pub struct Offset(u32);
#[derive(Debug, PartialEq)] pub struct Plain(u32);
#[derive(Debug, PartialEq)] pub struct WithOffset(u32, u32);
impl Plain { fn assume_offset(self, o: Offset) -> WithOffset { WithOffset(self.0, o.0) } }
impl Distribution<Offset> for Standard { fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> Offset { Offset(rng.next()) } }
impl Distribution<Plain> for Standard { fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> Plain { Plain(rng.next() * 10) } }
impl Distribution<WithOffset> for Standard {
    fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> WithOffset {
        let date_time: Plain = Self.sample(rng);
        date_time.assume_offset(Self.sample(rng))
    }
}
fn main() {
    let mut c = Counter(0);
    let v: WithOffset = Standard.sample(&mut c);
    assert_eq!(v, WithOffset(10, 2));
}
