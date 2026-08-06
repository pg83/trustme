// Extracted from library/alloc/src/sync.rs:1080
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    
    // Definition of a simple singly linked list using `Arc`:
    #[derive(Clone)]
    struct LinkedList<T>(Option<Arc<Node<T>>>);
    struct Node<T>(T, Option<Arc<Node<T>>>);
    
    // Dropping a long `LinkedList<T>` relying on the destructor of `Arc`
    // can cause a stack overflow. To prevent this, we can provide a
    // manual `Drop` implementation that does the destruction in a loop:
    impl<T> Drop for LinkedList<T> {
        fn drop(&mut self) {
            let mut link = self.0.take();
            while let Some(arc_node) = link.take() {
                if let Some(Node(_value, next)) = Arc::into_inner(arc_node) {
                    link = next;
                }
            }
        }
    }
    
    // Implementation of `new` and `push` omitted
    impl<T> LinkedList<T> {
        /* ... */
      fn new() -> Self {
          LinkedList(None)
      }
      fn push(&mut self, x: T) {
          self.0 = Some(Arc::new(Node(x, self.0.take())));
      }
    }
    
    // The following code could have still caused a stack overflow
    // despite the manual `Drop` impl if that `Drop` impl had used
    // `Arc::try_unwrap(arc).ok()` instead of `Arc::into_inner(arc)`.
    
    // Create a long list and clone it
    let mut x = LinkedList::new();
    let size = 100000;
    let size = if cfg!(miri) { 100 } else { size };
    for i in 0..size {
        x.push(i); // Adds i to the front of x
    }
    let y = x.clone();
    
    // Drop the clones in parallel
    let x_thread = std::thread::spawn(|| drop(x));
    let y_thread = std::thread::spawn(|| drop(y));
    x_thread.join().unwrap();
    y_thread.join().unwrap();
}
