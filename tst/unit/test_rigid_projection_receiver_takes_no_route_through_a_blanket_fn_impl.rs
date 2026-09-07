/* winnow `bits::take`: `input.iter_offsets().take(cnt + 1)` on a rigid
   `<I as Stream>::IterOffsets`.  `Parser::take` is only reachable through the
   blanket impl for `FnMut` closures, which a rigid projection cannot be; upstream
   drops that candidate and picks `Iterator::take`. */
trait Stream {
    type IterOffsets: Iterator<Item = (usize, u8)>;
    fn iter_offsets(&self) -> Self::IterOffsets;
}

trait Parser<I, O, E> {
    fn parse_next(&mut self, input: &mut I) -> Result<O, E>;
    fn take(self, _count: usize) -> usize
    where
        Self: Sized,
    {
        unreachable!()
    }
}

impl<I, O, E, F> Parser<I, O, E> for F
where
    F: FnMut(&mut I) -> Result<O, E>,
{
    fn parse_next(&mut self, input: &mut I) -> Result<O, E> {
        self(input)
    }
}

fn take_bytes<I: Stream>(input: &I, cnt: usize) -> usize {
    let mut acc = 0usize;
    for (_, byte) in input.iter_offsets().take(cnt + 1) {
        acc += byte as usize;
    }
    acc
}

struct Bytes(Vec<u8>);

impl Stream for Bytes {
    type IterOffsets = std::iter::Enumerate<std::vec::IntoIter<u8>>;
    fn iter_offsets(&self) -> Self::IterOffsets {
        self.0.clone().into_iter().enumerate()
    }
}

fn main() {
    assert_eq!(take_bytes(&Bytes(vec![1, 2, 3, 4]), 1), 3);
}
