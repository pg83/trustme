#![feature(extern_types)]
extern "C" { type Foreign; }
fn choose(choice: bool, value: &Foreign) -> u8 {
    match (choice, value) { (true | false, &_) => 17 }
}
fn main() {
    let function: fn(bool, &Foreign) -> u8 = choose;
    assert_eq!(std::mem::size_of_val(&function), std::mem::size_of::<usize>());
    assert_eq!(std::mem::size_of::<&Foreign>(), std::mem::size_of::<*const Foreign>());
}
