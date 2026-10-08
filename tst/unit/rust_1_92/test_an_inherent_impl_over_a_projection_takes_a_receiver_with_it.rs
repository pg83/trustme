// want depends on futures-util-preview, whose `FlattenStreamSink::poll_next`
// calls `self.as_mut().state().poll_future(cx)`. `poll_future` is in
// `impl<Fut> State<Fut, Fut::Ok> where Fut: TryFuture` and takes
// `self: Pin<&mut Self>`. Relating the receiver
// `Pin<&mut State<Args, <Args as TryFut>::Ok>>` to the declared
// `Pin<&mut State<?F, <?F as TryFut>::Ok>>` binds `?F = Args` at the first
// parameter, and the projection in the second is then decided. We still took
// `<?F as TryFut>::Ok` for open because its self was an inference variable,
// bound or not; the method stayed unresolved ("Spare rules left").
use std::pin::Pin;

trait TryFut {
    type Ok;
}

#[allow(dead_code)]
enum State<F, S> {
    Future(F),
    Stream(S),
    Done,
}

impl<F> State<F, F::Ok>
where
    F: TryFut,
{
    fn poll_future(self: Pin<&mut Self>) -> u32 {
        7
    }
}

struct Holder<F: TryFut> {
    state: State<F, F::Ok>,
}

impl<F: TryFut> Holder<F> {
    fn state(self: Pin<&mut Self>) -> Pin<&mut State<F, F::Ok>> {
        unsafe { self.map_unchecked_mut(|s| &mut s.state) }
    }

    fn poll_next(mut self: Pin<&mut Self>) -> u32 {
        self.as_mut().state().poll_future()
    }
}

struct X;

impl TryFut for X {
    type Ok = u8;
}

fn main() {
    let mut holder = Holder::<X> { state: State::Done };
    assert_eq!(Pin::new(&mut holder).poll_next(), 7);
}
