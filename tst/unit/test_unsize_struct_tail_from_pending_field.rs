// Unsizing a struct behind a pointer relates the pointer's marked parameter,
// here `Node<_>` against `Node<dyn Debug>`. The field's own type is still open
// while its expression waits on a coercion of its own, and equating the two
// sides decides that field to be the unsized target instead of leaving it to
// the expression that produces it. The struct's tail then had to be `dyn Debug`
// where the program says `&i32`, and the compiler rejected its own conclusion
// as an unsized value.

use std::fmt::Debug;

struct Node<T: ?Sized> {
    tail: T,
}

fn main() {
    let node: Box<Node<dyn Debug>> = Box::new(Node { tail: &1i32 });
    assert_eq!(format!("{:?}", &node.tail), "1");
}
