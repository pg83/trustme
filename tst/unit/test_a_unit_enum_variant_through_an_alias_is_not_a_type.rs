//@ compile-fail: Failed to find impl with 'Empty'
enum Entry { Empty }
type Alias = Entry;

fn main() {
    let _: Alias::Empty;
}
