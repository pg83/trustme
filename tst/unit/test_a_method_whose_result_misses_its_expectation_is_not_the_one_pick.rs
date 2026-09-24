// anstream's `StripStream::write` passes `&mut self.raw.as_locked_write()` to a
// `&mut dyn Write` parameter. The expectation `dyn Write` does not fit the
// method's `<S as AsLockedWrite>::Write<'_>`, which leaves the one candidate
// ambiguous on its result, not on its bounds. Only a candidate ambiguous on its
// method bounds alone is rustc's pick; picking this one committed
// `<S as AsLockedWrite>::Write == dyn Write` and the unsizing never happened.
use std::io::Write;

trait RawStream: Write {}
impl RawStream for Vec<u8> {}
impl<T: RawStream + ?Sized> RawStream for &mut T {}

trait AsLockedWrite {
    type Write<'w>: RawStream + 'w
    where
        Self: 'w;
    fn as_locked_write(&mut self) -> Self::Write<'_>;
}

impl AsLockedWrite for Vec<u8> {
    type Write<'w> = &'w mut Vec<u8>;
    fn as_locked_write(&mut self) -> Self::Write<'_> {
        self
    }
}

fn write(raw: &mut dyn Write, buf: &[u8]) -> std::io::Result<usize> {
    raw.write(buf)
}

struct Strip<S> {
    raw: S,
}

impl<S: Write + AsLockedWrite> Strip<S> {
    fn put(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        write(&mut self.raw.as_locked_write(), buf)
    }
}

fn main() {
    let mut s = Strip { raw: Vec::new() };
    s.put(b"abc").unwrap();
    assert_eq!(s.raw, b"abc");

}
