//@ proc-macro-aux-build: preserve_bound_types.rs
use preserve_bound_types::preserve;

#[preserve]
fn callback<'a>(bias: &'a u16) -> impl for<'b> Fn(&'b u16) -> u16 + Copy + Send + 'a + use<'a> {
    move |value| *value + *bias
}

#[preserve]
fn text() -> &'static (impl core::fmt::Debug + ?Sized + use<>) { "kept" }

fn main() {
    let bias = 5;
    let first = callback(&bias);
    let second = first;
    assert_eq!((first(&11), second(&13)), (16, 18));
    assert_eq!(format!("{:?}", text()), "\"kept\"");
}
