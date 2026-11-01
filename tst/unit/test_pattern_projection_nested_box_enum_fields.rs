#![feature(box_patterns)]

enum Tree<T> { Leaf(T), Pair(Box<Tree<T>>, Box<Tree<T>>) }

fn read(tree: Box<Tree<(u8, u16)>>) -> u16 {
    match tree {
        box Tree::Pair(box Tree::Leaf((first, _)), box Tree::Leaf((_, last))) => first as u16 + last,
        box Tree::Leaf((first, last)) => first as u16 * last,
        _ => 0,
    }
}

fn main() {
    let tree = Box::new(Tree::Pair(Box::new(Tree::Leaf((7, 11))), Box::new(Tree::Leaf((13, 257)))));
    assert_eq!(read(std::hint::black_box(tree)), 264);
    assert_eq!(read(Box::new(Tree::Leaf((3, 17)))), 51);
}
