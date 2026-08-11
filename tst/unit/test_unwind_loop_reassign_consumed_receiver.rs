#![feature(lang_items, no_core)]
#![no_core]
#![no_main]
//@ compile-flags: -O -Zmir-opt-level=1 -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "legacy_receiver"]
pub trait LegacyReceiver: PointeeSized {}

impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for i32 {}

#[lang = "eq"]
pub trait PartialEq<Rhs = Self> {
    fn eq(&self, other: &Rhs) -> bool;
}

impl PartialEq for i32 {
    fn eq(&self, other: &i32) -> bool {
        *self == *other
    }
}

#[lang = "coerce_unsized"]
pub trait CoerceUnsized<T> {}

#[lang = "drop"]
pub trait Drop {
    fn drop(&mut self);
}

#[lang = "destruct"]
pub trait Destruct {}

#[lang = "drop_in_place"]
pub unsafe fn drop_in_place<T: PointeeSized>(_to_drop: *mut T) {}

#[lang = "panic_in_cleanup"]
fn panic_in_cleanup() -> ! {
    loop {}
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

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    match next(Edge { value: 3, index: 0 }) {
        Outcome::Ok(_) => 1,
        Outcome::Err(_) => 0,
    }
}
