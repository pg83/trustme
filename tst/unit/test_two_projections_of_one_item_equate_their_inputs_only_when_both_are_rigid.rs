//@ run-pass
// proptest 1.11 `strategy/shuffle.rs:142`: `self.current()` in an inherent impl of
// `ShuffleValueTree<V>` whose where-clause `V::Value: Shuffleable` matches the `ValueTree`
// impl's.  The method proof met `<ShuffleValueTree<V> as ValueTree>::Value` against
// `<V as ValueTree>::Value` (the return type against the impl's item) and, reading two
// projections of one item as equal through their inputs, asked `ShuffleValueTree<V> == V`.
// The first normalizes through the impl; only two rigid projections relate that way.
pub trait Shuffleable {
    fn shuffle_len(&self) -> usize;
}

impl<T> Shuffleable for Vec<T> {
    fn shuffle_len(&self) -> usize {
        self.len()
    }
}

pub trait ValueTree {
    type Value;
    fn current(&self) -> Self::Value;
}

pub struct Just<T>(T);

impl<T: Clone> ValueTree for Just<T> {
    type Value = T;
    fn current(&self) -> T {
        self.0.clone()
    }
}

pub struct ShuffleValueTree<V> {
    inner: V,
    dist: std::cell::Cell<Option<usize>>,
}

impl<V: ValueTree> ShuffleValueTree<V>
where
    V::Value: Shuffleable,
{
    fn init_dist(&self, dflt: usize) -> usize {
        if self.dist.get().is_none() {
            self.dist.set(Some(dflt));
        }
        self.dist.get().unwrap()
    }

    fn force_init_dist(&self) {
        if self.dist.get().is_none() {
            self.init_dist(self.current().shuffle_len());
        }
    }
}

impl<V: ValueTree> ValueTree for ShuffleValueTree<V>
where
    V::Value: Shuffleable,
{
    type Value = V::Value;
    fn current(&self) -> V::Value {
        self.inner.current()
    }
}

impl<V: ValueTree> ShuffleValueTree<V>
where
    V::Value: Shuffleable,
{
    pub fn simplify(&self) -> bool {
        self.force_init_dist();
        self.dist.get().unwrap() > 0
    }
}

fn main() {
    let tree = ShuffleValueTree { inner: Just(vec![1, 2, 3]), dist: std::cell::Cell::new(None) };
    assert_eq!(tree.current(), vec![1, 2, 3]);
    assert!(tree.simplify());
    assert_eq!(tree.dist.get(), Some(3));
}
