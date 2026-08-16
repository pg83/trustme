// A derived `Hash` writes an enum's discriminant at the width its `#[repr]`
// names. It always wrote a `usize`, so a `#[repr(u8)]` enum fed eight bytes to
// the hasher instead of one.
//
// Same shape as the upstream test deriving/deriving-hash.rs.
use std::hash::{Hash, Hasher};

struct Collector(Vec<u8>);

impl Hasher for Collector {
    fn finish(&self) -> u64 {
        unreachable!()
    }
    fn write(&mut self, bytes: &[u8]) {
        self.0.extend_from_slice(bytes);
    }
}

fn bytes<T: Hash>(v: &T) -> Vec<u8> {
    let mut c = Collector(Vec::new());
    v.hash(&mut c);
    c.0
}

#[repr(u8)]
#[derive(Hash)]
enum Narrow {
    A,
    B,
}

#[repr(u16)]
#[derive(Hash)]
enum Medium {
    A,
    B,
}

#[derive(Hash)]
enum Plain {
    A,
    B,
}

#[derive(Hash)]
enum Single {
    A(u8),
}

fn main() {
    // One byte for the discriminant, and the two variants differ.
    assert_eq!(bytes(&Narrow::A), vec![0]);
    assert_eq!(bytes(&Narrow::B), vec![1]);

    assert_eq!(bytes(&Medium::A).len(), 2);
    assert_ne!(bytes(&Medium::A), bytes(&Medium::B));

    // A default enum's discriminant is pointer-sized.
    assert_eq!(bytes(&Plain::A).len(), std::mem::size_of::<isize>());
    assert_ne!(bytes(&Plain::A), bytes(&Plain::B));

    // A single-variant enum hashes no discriminant at all.
    assert_eq!(bytes(&Single::A(17)), vec![17]);
}
