/* futures-util-0.3.17 `src/sink/with.rs`: `ready!(self.project().sink.poll_ready(cx)?)`
 * inside `impl<Si, Item, U, Fut, F, E> Sink<U> for With<..> where E: From<Si::Error>`.
 *
 * The `?` expands to `FromResidual::from_residual(residual)`, whose parameter `R` and
 * whose `Self` are both fresh variables.  Upstream `check_argument_types` coerces the
 * argument into that parameter before anything selects the call's own obligations -
 * `select_obligations_where_possible` runs there only between the two `check_closures`
 * rounds, and a goal whose self type is still an inference variable makes no progress
 * anyway (`assemble_candidates` takes its fast path out and reports ambiguity).  A
 * coercion into a parameter that is a bare inference variable is a unification:
 * `Coerce::coerce` bails out of `coerce_unsized` on such a target, matches no case of
 * the target's shape and ends in `self.unify(a, b)`.  So `R` is
 * `Result<Infallible, Si::Error>` before `Poll<Result<(), E>>: FromResidual<R>` is
 * ever selected, and the nested `E: From<Si::Error>` the impl asks for is the
 * where-clause.
 *
 * Selecting that obligation first instead left `R` open: the impl's own `E` stayed a
 * fresh existential, the nested `E: From<?E>` was decided from the candidates in
 * scope - the reflexive `impl<T> From<T> for T` - and binding `?E = E` made the
 * pending coercion clash, reported as a mismatch between `E` and `Si::Error`. */
use std::marker::PhantomData;
use std::task::Poll;

macro_rules! ready {
    ($e:expr) => {
        match $e {
            Poll::Ready(t) => t,
            Poll::Pending => return Poll::Pending,
        }
    };
}

trait Sink<Item> {
    type Error;
    fn poll_ready(&mut self) -> Poll<Result<(), Self::Error>>;
}

struct With<Si, Item, E> {
    sink: Si,
    _p: PhantomData<fn(Item) -> E>,
}

impl<Si, Item, E> With<Si, Item, E>
where
    Si: Sink<Item>,
    E: From<Si::Error>,
{
    fn poll_ready(&mut self) -> Poll<Result<(), E>> {
        ready!(self.sink.poll_ready()?);
        Poll::Ready(Ok(()))
    }
}

struct S;
#[derive(Debug)]
struct SErr;
#[derive(Debug)]
struct WErr;
impl From<SErr> for WErr {
    fn from(_: SErr) -> Self {
        WErr
    }
}
impl Sink<u8> for S {
    type Error = SErr;
    fn poll_ready(&mut self) -> Poll<Result<(), SErr>> {
        Poll::Ready(Ok(()))
    }
}

fn main() {
    let mut w: With<S, u8, WErr> = With { sink: S, _p: PhantomData };
    match w.poll_ready() {
        Poll::Ready(Ok(())) => {}
        _ => panic!("unexpected"),
    }
}
