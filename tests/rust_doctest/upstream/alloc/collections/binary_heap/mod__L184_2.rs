// Extracted from library/alloc/src/collections/binary_heap/mod.rs:184
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    
    // Type inference lets us omit an explicit type signature (which
    // would be `BinaryHeap<i32>` in this example).
    let mut heap = BinaryHeap::new();
    
    // We can use peek to look at the next item in the heap. In this case,
    // there's no items in there yet so we get None.
    assert_eq!(heap.peek(), None);
    
    // Let's add some scores...
    heap.push(1);
    heap.push(5);
    heap.push(2);
    
    // Now peek shows the most important item in the heap.
    assert_eq!(heap.peek(), Some(&5));
    
    // We can check the length of a heap.
    assert_eq!(heap.len(), 3);
    
    // We can iterate over the items in the heap, although they are returned in
    // a random order.
    for x in &heap {
        println!("{x}");
    }
    
    // If we instead pop these scores, they should come back in order.
    assert_eq!(heap.pop(), Some(5));
    assert_eq!(heap.pop(), Some(2));
    assert_eq!(heap.pop(), Some(1));
    assert_eq!(heap.pop(), None);
    
    // We can clear the heap of any remaining items.
    heap.clear();
    
    // The heap should now be empty.
    assert!(heap.is_empty())
}
