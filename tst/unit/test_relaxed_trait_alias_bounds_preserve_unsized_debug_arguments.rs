#![feature(trait_alias)]

trait View = ?Sized + std::fmt::Debug;
fn render<T: ?Sized + View>(value: &T) -> String { format!("{:?}", value) }

fn main() {
    assert_eq!(render("word"), "\"word\"");
    assert_eq!(render(&[11u32, 17][..]), "[11, 17]");
}
