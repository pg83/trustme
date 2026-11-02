//@ compile-variant: -O
use std::mem::ManuallyDrop;
union Choice { text: ManuallyDrop<String>, empty: () }
fn main() {
    let choice = Choice { text: ManuallyDrop::new(String::from("held")) };
    let text = unsafe { let Choice { text } = choice; text };
    let text = ManuallyDrop::into_inner(text);
    assert_eq!(text, "held");
}
