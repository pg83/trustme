use std::ptr;

struct Node {
    next: *mut Self,
}

impl Node {
    fn new() -> Self {
        Self {
            next: ptr::null_mut(),
        }
    }
}

fn main() {
    let node = Node::new();
    assert!(node.next.is_null());
}
