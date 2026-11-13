//@ compile-fail: didn't point to a struct or union (Enum)
enum Payload {
    Item(u8),
}
type Alias = Payload;
#[allow(non_snake_case)]
fn Alias() {}

fn main() {
    let Alias(_) = Payload::Item(7);
}
