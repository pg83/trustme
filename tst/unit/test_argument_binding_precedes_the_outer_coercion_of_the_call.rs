/* futures 0.1 `try_ready!`: `return Err(From::from(e))` with `e: T::SinkError` under
   a where-clause `T::SinkError: From<U::Error>`.  Upstream binds the argument first,
   so the goal is `T::SinkError: From<T::SinkError>`, met by the reflexive impl; the
   where-clause must not be chosen while the argument's variable is still open. */
trait Sink {
    type SinkItem;
    type SinkError;
    fn start_send(&mut self, item: Self::SinkItem) -> Result<(), Self::SinkError>;
}

trait Stream {
    type Item;
    type Error;
    fn poll(&mut self) -> Result<Option<Self::Item>, Self::Error>;
}

fn send_all<T, U>(sink: &mut T, stream: &mut U) -> Result<usize, T::SinkError>
where
    T: Sink,
    U: Stream<Item = T::SinkItem>,
    T::SinkError: From<U::Error>,
{
    let mut sent = 0;
    while let Some(item) = stream.poll()? {
        match sink.start_send(item) {
            Ok(()) => sent += 1,
            Err(e) => return Err(From::from(e)),
        }
    }
    Ok(sent)
}

struct Counter(usize);
struct Full;

impl Sink for Counter {
    type SinkItem = u8;
    type SinkError = Full;
    fn start_send(&mut self, _item: u8) -> Result<(), Full> {
        if self.0 == 2 {
            return Err(Full);
        }
        self.0 += 1;
        Ok(())
    }
}

struct Bytes(Vec<u8>);
struct Broken;

impl From<Broken> for Full {
    fn from(_: Broken) -> Full {
        Full
    }
}

impl Stream for Bytes {
    type Item = u8;
    type Error = Broken;
    fn poll(&mut self) -> Result<Option<u8>, Broken> {
        Ok(self.0.pop())
    }
}

fn main() {
    let mut sink = Counter(0);
    assert!(send_all(&mut sink, &mut Bytes(vec![1, 2])).is_ok());
    assert!(send_all(&mut sink, &mut Bytes(vec![3])).is_err());
}
