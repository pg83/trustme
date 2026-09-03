struct Heap<T> {
    items: Vec<T>,
}

impl<T: Ord> Heap<T> {
    fn new() -> Heap<T> {
        Heap { items: Vec::new() }
    }

    fn reserve_exact(&mut self, additional: usize) {
        self.items.reserve_exact(additional);
    }

    fn push(&mut self, value: T) {
        self.items.push(value);
    }
}

fn main() {
    let mut heap = Heap::new();
    heap.reserve_exact(100);
    heap.push(4);
    assert_eq!(heap.items.len(), 1);
    assert!(heap.items.capacity() >= 100);
}
