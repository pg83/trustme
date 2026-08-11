#![feature(lang_items, no_core, start)]
#![no_core]
//@ compile-flags: -O -Zmir-opt-level=1

#[lang = "sized"]
pub trait Sized {}

#[lang = "coerce_unsized"]
pub trait CoerceUnsized<T> {}

#[lang = "drop"]
pub trait Drop {
    fn drop(&mut self);
}

struct Edge<T> {
    value: T,
    index: i32,
}

struct Node<T>(T);
struct Item<T>(T);

enum Outcome<T, E> {
    Ok(T),
    Err(E),
}

impl<T> Edge<T> {
    fn right(self) -> Outcome<Item<T>, Edge<T>> {
        if self.index == 0 {
            Outcome::Err(self)
        } else {
            Outcome::Ok(Item(self.value))
        }
    }

    fn into_node(self) -> Node<T> {
        Node(self.value)
    }
}

impl<T> Node<T> {
    fn ascend(self) -> Outcome<Edge<T>, Node<T>> {
        Outcome::Err(self)
    }
}

fn next<T>(mut edge: Edge<T>) -> Outcome<Item<T>, Node<T>> {
    loop {
        edge = match edge.right() {
            Outcome::Ok(item) => return Outcome::Ok(item),
            Outcome::Err(last_edge) => match last_edge.into_node().ascend() {
                Outcome::Ok(parent_edge) => parent_edge,
                Outcome::Err(root) => return Outcome::Err(root),
            },
        };
    }
}

fn main() -> i32 {
    match next(Edge { value: 3, index: 0 }) {
        Outcome::Ok(_) => 1,
        Outcome::Err(_) => 0,
    }
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
