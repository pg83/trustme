const fn tag(value: u8) -> isize {
    (value as isize) + 2
}

enum Choice {
    First = tag(3),
    Last = tag(8),
}

#[repr(u8)]
enum Payload {
    Empty = 4,
    Value(u16) = 9,
}

struct Frame<const N: usize> {
    bytes: [u8; N],
}

trait Read {
    const OFFSET: usize;
    fn read(&self) -> u8;
}

impl<const N: usize> Read for Frame<N> {
    const OFFSET: usize = 1;
    fn read(&self) -> u8 {
        self.bytes[Self::OFFSET]
    }
}

static STORED: Frame<{ tag(1) as usize }> = Frame { bytes: [5, 17, 29] };
const BORROWED: &[u8; 2] = &[11, 23];

fn main() {
    let read = || <Frame<3> as Read>::read(&STORED);
    assert_eq!(read(), 17);
    assert_eq!(BORROWED[1], 23);
    assert_eq!(Choice::First as isize, 5);
    assert_eq!(Choice::Last as isize, 10);
    assert!(matches!(Payload::Empty, Payload::Empty));
    assert_eq!(match Payload::Value(31) { Payload::Value(v) => v, Payload::Empty => 0 }, 31);
}
