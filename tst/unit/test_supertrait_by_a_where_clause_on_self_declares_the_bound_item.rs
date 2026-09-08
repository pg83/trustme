//@ run-pass
/* plotters 0.3.7 `coord/ranged1d/combinators/group_by.rs`: `impl<T, R: DiscreteRanged<ValueType = T> +
   ValueFormatter<T>> ValueFormatter<T> for GroupBy<R>` with `trait DiscreteRanged where Self:
   Ranged` - the binding names `Ranged`'s `ValueType` through a supertrait written as a
   where-clause on `Self`; the lowering looked only past the colon, left the binding's source
   trait empty, and the bind pass asserted on the empty path ("Invalid path (no nodes) - ::"). */
pub trait Ranged {
    type ValueType;
    fn range(&self) -> (Self::ValueType, Self::ValueType);
}

pub trait DiscreteRanged
where
    Self: Ranged,
{
    fn size(&self) -> usize;
}

pub trait ValueFormatter<V> {
    fn format(value: &V) -> String;
}

pub struct GroupBy<R>(R, usize);

impl<T, R: DiscreteRanged<ValueType = T> + ValueFormatter<T>> ValueFormatter<T> for GroupBy<R> {
    fn format(value: &T) -> String {
        R::format(value)
    }
}

struct Ints(i32, i32);

impl Ranged for Ints {
    type ValueType = i32;
    fn range(&self) -> (i32, i32) {
        (self.0, self.1)
    }
}

impl DiscreteRanged for Ints {
    fn size(&self) -> usize {
        (self.1 - self.0) as usize
    }
}

impl ValueFormatter<i32> for Ints {
    fn format(value: &i32) -> String {
        format!("<{value}>")
    }
}

fn main() {
    let g = GroupBy(Ints(0, 10), 2);
    assert_eq!(<GroupBy<Ints> as ValueFormatter<i32>>::format(&7), "<7>");
    assert_eq!(g.0.size(), 10);
}
