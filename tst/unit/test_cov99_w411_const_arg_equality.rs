struct Tag<const N: usize>;

trait Width<T> {
    const N: usize;
}

impl<const K: usize> Width<Tag<K>> for () {
    const N: usize = K;
}

fn take<const N: usize>(_: Tag<N>) {}

fn main() {
    take::<{ <() as Width<Tag<{ 1 + 2 }>>>::N }>(Tag::<3>);
}
