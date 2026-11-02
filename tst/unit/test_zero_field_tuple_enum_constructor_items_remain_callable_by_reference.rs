//@ compile-variant: -O

enum Choice { Empty() }
fn invoke<F: Fn() -> Choice>(call: F) -> Choice { call() }

fn main() {
    let item = Choice::Empty;
    assert_eq!(std::mem::size_of_val(&item), 0);
    assert!(matches!(invoke(&item), Choice::Empty()));
    assert!(matches!(invoke(Choice::Empty), Choice::Empty()));
}
