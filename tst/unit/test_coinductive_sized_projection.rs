//@ run-pass

trait Factory<T> {
    type Collection;
}

impl<T> Factory<T> for Vec<()> {
    type Collection = Vec<T>;
}

struct Node<C: Factory<Self>> {
    children: C::Collection,
}

fn main() {
    let node = Node::<Vec<()>> {
        children: Vec::new(),
    };
    assert!(node.children.is_empty());
}
