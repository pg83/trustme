/* syn's `Iter`/`IterMut` shape: a trait whose declaration pins a supertrait's
   associated type, then used as a trait object. The object type elaborates the
   supertrait bound, so it carries `Item = ..` even where nothing wrote it - and
   that binding has no vtable type index by construction, because the
   declaration fixed it for every implementor. */
pub trait IterTrait<'a, T: 'a>: Iterator<Item = &'a T> + ExactSizeIterator {
    fn first_len(&self) -> usize;
}

impl<'a, T: 'a, I> IterTrait<'a, T> for I
where
    I: Iterator<Item = &'a T> + ExactSizeIterator,
{
    fn first_len(&self) -> usize {
        self.len()
    }
}

fn sum_through_object(it: &mut (dyn IterTrait<'_, u32> + '_)) -> u32 {
    let mut total = 0;
    while let Some(v) = it.next() {
        total += *v;
    }
    total
}

fn main() {
    let values = [1u32, 2, 3, 4];
    let mut iter = values.iter();
    let object: &mut (dyn IterTrait<'_, u32> + '_) = &mut iter;
    assert_eq!(object.first_len(), 4);
    assert_eq!(sum_through_object(object), 10);

    /* The binding spelled out by hand, as syn writes it for `IterMut`. */
    let mut other = values.iter();
    let named: &mut (dyn IterTrait<'_, u32, Item = &u32> + '_) = &mut other;
    assert_eq!(named.first_len(), 4);
}
