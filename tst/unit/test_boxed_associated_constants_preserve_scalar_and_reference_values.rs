//@ compile-variant: -O
trait Data { const NUMBER: u32; const TEXT: &'static str; }
struct Source;
impl Data for Source { const NUMBER: u32 = 101; const TEXT: &'static str = "kept"; }
fn main() {
    let number = Box::new(<Source as Data>::NUMBER);
    let text = Box::new(<Source as Data>::TEXT);
    assert_eq!(*number, 101);
    assert_eq!(*text, "kept");
}
