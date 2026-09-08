struct SmallVec<T, const N: usize> {
    buf: [T; N],
    len: usize,
}

impl<T, const N: usize> core::ops::Deref for SmallVec<T, N> {
    type Target = [T];
    fn deref(&self) -> &[T] {
        &self.buf[..self.len]
    }
}

struct Axis {
    widths: SmallVec<i32, 4>,
}

fn count(w: &[i32]) -> i32 {
    w.len() as i32
}

fn negated(w: &[i32]) -> i32 {
    -(w.len() as i32)
}

fn main() {
    let cond = std::env::args().count() > 5;
    let f = if cond { count } else { negated };
    let mut g = |axis: &Axis| f(&axis.widths);
    let ax = Axis { widths: SmallVec { buf: [1, 2, 3, 4], len: 2 } };
    assert_eq!(g(&ax), -2);
}
