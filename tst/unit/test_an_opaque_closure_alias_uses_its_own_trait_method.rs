#![feature(type_alias_impl_trait)]

type Action = impl Fn(u8) -> u8 + Copy;

trait Run {
    fn run(&self, input: u8) -> u8;
}

impl Run for Action {
    fn run(&self, input: u8) -> u8 {
        self(input) + 1
    }
}

#[define_opaque(Action)]
fn action(offset: u8) -> Action {
    move |input| input + offset
}

fn main() {
    let first = action(3);
    let copied = first;
    assert_eq!(first.run(7), 11);
    assert_eq!(copied.run(9), 13);
}
