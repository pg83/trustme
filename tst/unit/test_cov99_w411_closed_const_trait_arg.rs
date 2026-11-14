struct Tag<const N: usize>;

trait Width<T> {
    const N: usize;
}

impl<const K: usize> Width<Tag<K>> for () {
    const N: usize = K;
}

fn main() {
    let value: [u8; 3] = [7; <() as Width<Tag<{ 1 + 2 }>>>::N];
    assert_eq!(value, [7; 3]);
}
