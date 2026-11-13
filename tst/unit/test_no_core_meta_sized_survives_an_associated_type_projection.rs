//@ crate-type: lib
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

pub trait View {
    type Value: MetaSized + ?Sized;
}

pub struct Text;

impl View for Text {
    type Value = str;
}

pub fn require_metadata<T: MetaSized + ?Sized>() {}

pub fn through_projection<V: View>() {
    require_metadata::<V::Value>();
}

pub fn projected_metadata_witness() {
    through_projection::<Text>();
}
