//@ run-pass
/* combine-4.6.4 `parser::range::length_prefix` (range.rs:636-669).  `O` and `E` of the
   `AndThen` impl are named only by that impl's own where-clauses - `F: FnMut(P::Output)
   -> Result<O, E>` (RFC 447) and `E: Into<I::Err>` - and the closure's return type is
   still open when `&mut parser` is coerced to `&mut dyn Parser<I, Output = usize>`.

   Upstream never instantiates a candidate it has not decided on.  `Coerce::coerce_unsized`
   (rustc_hir_typeck/src/coercion.rs) grows the coercion's obligation set from
   `impl_source.nested` only under `Ok(Some(ImplSource::UserDefined(..)))`; its `Ok(None)`
   arm re-queues the goal exactly as it was or abandons the coercion, the same answer
   `FulfillProcessor::process_trait_obligation` (rustc_trait_selection/src/traits/fulfill.rs)
   gives an ambiguous goal - `ProcessResult::Unchanged`, no obligations left behind.

   Here the coercion-checking sweep re-asks every unconsumed coercion once a pass, and
   taking the where-clauses of the undecided `AndThen` candidate minted a fresh `O`/`E`
   pair each time.  Nothing can relate one pass's `E` to the next's, so no rule was ever
   deduplicated or discharged; each new rule marked a change, and the pass ended before
   the stage that consumes coercions - the one that gives the closure its return type and
   closes the loop.  Typeck ran out its 5000 iterations leaving tens of thousands of
   spare rules behind. */
use std::num::TryFromIntError;

trait StdErr {}
impl StdErr for TryFromIntError {}

trait Mk {
    fn other<E: StdErr>(e: E) -> Self;
}
trait Stream {
    type Err: Mk;
}

trait Parser<I: Stream> {
    type Output;
    fn parse(&mut self) -> Self::Output;
    fn and_then<F, O, E>(self, f: F) -> AndThen<Self, F>
    where
        Self: Sized,
        F: FnMut(Self::Output) -> Result<O, E>,
        E: Into<I::Err>,
    {
        AndThen(self, f)
    }
}

struct AndThen<P, F>(P, F);
impl<I: Stream, P: Parser<I>, F, O, E> Parser<I> for AndThen<P, F>
where
    F: FnMut(P::Output) -> Result<O, E>,
    E: Into<I::Err>,
{
    type Output = O;
    fn parse(&mut self) -> O {
        match (self.1)(self.0.parse()) {
            Ok(o) => o,
            Err(_) => panic!("conversion failed"),
        }
    }
}

fn length_prefix<I: Stream, P: Parser<I>>(len: P) -> usize
where
    usize: TryFrom<P::Output>,
    <usize as TryFrom<P::Output>>::Error: StdErr,
{
    let mut parser = len.and_then(|u| usize::try_from(u).map_err(<I::Err as Mk>::other));
    let taken: &mut dyn Parser<I, Output = usize> = &mut parser;
    taken.parse()
}

struct Failure;
impl Mk for Failure {
    fn other<E: StdErr>(_e: E) -> Self {
        Failure
    }
}

struct Bytes;
impl Stream for Bytes {
    type Err = Failure;
}

struct Length(u64);
impl Parser<Bytes> for Length {
    type Output = u64;
    fn parse(&mut self) -> u64 {
        self.0
    }
}

fn main() {
    assert_eq!(length_prefix::<Bytes, Length>(Length(7)), 7);
}
