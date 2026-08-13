trait Link {
    fn next(&self) -> Option<&dyn Link> {
        None
    }
}

struct Node {
    next: Option<Box<Node>>,
}

impl Link for Node {
    fn next(&self) -> Option<&dyn Link> {
        self.next.as_deref().map(|node| node as &dyn Link)
    }
}

fn main() {
    let node = Node { next: Some(Box::new(Node { next: None })) };
    let node: &dyn Link = &node;
    assert!(node.next().is_some());
    assert!(Some(node).and_then(Link::next).is_some());
}
