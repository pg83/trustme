// `map(|row| row.iter().collect_array())` leaves the closure's return
// variable open until `Map<_, closure>: CollectArray<?A>` is proven through
// `impl<A, I> CollectArray<A> for I where I: Iterator<Item = A>`, whose
// projection is the closure's output.  Upstream's closure type carries its
// signature as generic arguments, so the return variable is an input of the
// canonical goal and the answer `?A = <closure output>` reaches the caller as
// an equality between its own variables.  Here the closure type is the node;
// its signature has to be canonicalized with the goal for the same to hold,
// or the caller's variable is bound to a canonical input of a nested goal and
// `?A` is never linked to the closure's output.

use std::mem::MaybeUninit;
const N: usize = 2;

trait CollectArray<A>: Iterator<Item = A> {
    fn inner_array(&mut self) -> [A; N];
    fn collect_array(&mut self) -> [A; N] {
        let result = self.inner_array();
        assert!(self.next().is_none());
        result
    }
}

impl<A, I: ?Sized> CollectArray<A> for I
where
    I: Iterator<Item = A>,
{
    fn inner_array(&mut self) -> [A; N] {
        let mut result: [MaybeUninit<A>; N] = unsafe { MaybeUninit::uninit().assume_init() };
        for (dest, item) in result.iter_mut().zip(self) {
            *dest = MaybeUninit::new(item);
        }
        let temp_ptr: *const [MaybeUninit<A>; N] = &result;
        unsafe { std::ptr::read(temp_ptr as *const [A; N]) }
    }
}

fn main() {
    assert_eq!(
        [[1, 2], [3, 4]]
            .iter()
            .map(|row| row.iter().collect_array())
            .collect_array(),
        [[&1, &2], [&3, &4]]
    );
}
