/* arrayvec `extend_from_slice`: `self.extend_from_iter::<_, false>(slice.iter().cloned())`
   names the iterator parameter `_` explicitly; the argument still binds it, and the
   where-clause `I: IntoIterator<Item = T>` is met by it. */
struct Vec2<T> {
    items: Vec<T>,
    cap: usize,
}

impl<T> Vec2<T> {
    fn capacity(&self) -> usize {
        self.cap
    }
    fn len(&self) -> usize {
        self.items.len()
    }
    unsafe fn extend_from_iter<I, const CHECK: bool>(&mut self, iterable: I)
    where
        I: IntoIterator<Item = T>,
    {
        let take = self.capacity() - self.len();
        for item in iterable.into_iter().take(take) {
            self.items.push(item);
        }
    }
    fn extend_from_slice(&mut self, slice: &[T])
    where
        T: Clone,
    {
        let take = self.capacity() - self.len();
        unsafe {
            let slice = if take < slice.len() { &slice[..take] } else { slice };
            self.extend_from_iter::<_, false>(slice.iter().cloned());
        }
    }
}

fn main() {
    let mut v = Vec2 { items: Vec::new(), cap: 3 };
    v.extend_from_slice(&[1u8, 2, 3, 4]);
    assert_eq!(v.items, vec![1, 2, 3]);
}
