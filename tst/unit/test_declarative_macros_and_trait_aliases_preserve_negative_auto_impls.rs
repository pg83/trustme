#![feature(decl_macro, trait_alias, auto_traits, negative_impls)]
auto trait Allowed {}
struct Hidden;
impl !Allowed for Hidden {}
trait Text = AsRef<str> + Clone;
pub macro width($value:expr) { $value.as_ref().len() }
fn read<T: Text + Allowed>(value: T) -> usize { width!(value) }
fn main() {
    assert_eq!(read(String::from("field")), 5);
    let _ = Hidden;
}
