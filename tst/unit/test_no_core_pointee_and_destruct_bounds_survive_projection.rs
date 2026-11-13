//@ crate-type: lib
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "destruct"]
pub trait Destruct: PointeeSized {}

pub trait View {
    type Value: Destruct + ?Sized;
}

pub struct Text;
impl View for Text {
    type Value = str;
}

pub fn require_bounds<T: PointeeSized + Destruct + ?Sized>() {}

pub fn forward_bounds<T: Destruct + ?Sized>() {
    require_bounds::<T>();
}

pub fn projected_bounds<V: View>() {
    require_bounds::<V::Value>();
}

pub fn bound_witnesses() {
    forward_bounds::<[u16]>();
    projected_bounds::<Text>();
    require_bounds::<fn()>();
}
