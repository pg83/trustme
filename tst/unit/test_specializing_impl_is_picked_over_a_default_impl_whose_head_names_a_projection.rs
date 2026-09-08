//@ run-pass
/* `LinkedList::extend` with another list: `<Self as SpecExtend<I>>::spec_extend` chooses between
   alloc's default `impl<I: IntoIterator, A> SpecExtend<I> for LinkedList<I::Item, A>` and the
   specializing `impl<T> SpecExtend<LinkedList<T>> for LinkedList<T>`.  On `LinkedList<Vec<i32>>`
   the default impl's head is `LinkedList<<LinkedList<Vec<i32>> as IntoIterator>::Item, Global>`
   - the same head once the projection is normalized; read raw, the two impls were never
   compared, both stayed viable, and the code generator found no `spec_extend` ("Item not
   found").  rayon's `LinkedList<Vec<_>>` collection tests hit it. */
use std::collections::LinkedList;

fn main() {
    let mut a: LinkedList<Vec<i32>> = LinkedList::new();
    a.push_back(vec![1]);
    let mut b: LinkedList<Vec<i32>> = LinkedList::new();
    b.push_back(vec![2, 3]);
    a.extend(b);
    assert_eq!(a.len(), 2);
    assert_eq!(a.back().unwrap(), &vec![2, 3]);
    let mut c: LinkedList<u8> = LinkedList::new();
    c.extend(vec![7, 8]);
    c.extend(LinkedList::from([9]));
    assert_eq!(c.iter().copied().collect::<Vec<u8>>(), vec![7, 8, 9]);
}
