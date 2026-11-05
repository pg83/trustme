use std::marker::PhantomData;

trait Select<V> {
    type Output;
}
struct Gate<T>(PhantomData<T>);
impl<V> Select<V> for Gate<u16> {
    type Output = V;
}

fn combine<T, V>(first: T, second: V, third: <Gate<T> as Select<V>>::Output)
    -> (T, V, <Gate<T> as Select<V>>::Output)
where
    Gate<T>: Select<V>,
{
    (first, second, third)
}

fn main() {
    let earlier = Default::default();
    let number = 3;
    let (first, second, third) = combine(earlier, number, earlier);
    assert_eq!(first, 0);
    assert_eq!(second, 3);
    assert_eq!(third, 0);
    assert_eq!(std::mem::size_of_val(&first), 2);
    assert_eq!(std::mem::size_of_val(&second), 2);
    assert_eq!(std::mem::size_of_val(&third), 2);
}
