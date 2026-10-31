//@ aux-build: foreign_trait_object_aliases.rs
#![feature(trait_alias)]

extern crate foreign_trait_object_aliases;

fn main() {
    let bytes: Box<dyn foreign_trait_object_aliases::Bytes> =
        Box::new([19_u8, 23, 31].into_iter());
    assert_eq!(bytes.collect::<Vec<_>>(), vec![19, 23, 31]);

    let shown: Box<dyn foreign_trait_object_aliases::Show> = Box::new((17_u16, false));
    assert_eq!(format!("{shown:?}"), "(17, false)");
}
