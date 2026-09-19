// A subslice binding borrows the element the subslice starts at and reads the
// rest of the array through that one pointer, so splitting the array into a
// local per element leaves the pointer addressing a lone element. The start is
// not always element 0: `[_, ref x @ .., _]` starts at element 1.
#[derive(Clone, Copy, PartialEq, Debug)]
struct Copyable(u8);

#[derive(PartialEq, Debug)]
struct Owned(u8);

const fn copyable() -> &'static [Copyable; 2] {
    let [_, ref x @ .., _] = [Copyable(1), Copyable(2), Copyable(3), Copyable(4)];
    x
}

const fn owned() -> &'static [Owned; 2] {
    let [_, ref x @ .., _] = [Owned(1), Owned(2), Owned(3), Owned(4)];
    x
}

static COPYABLE: &'static [Copyable; 2] = copyable();
static OWNED: &'static [Owned; 2] = owned();

fn main() {
    assert_eq!(COPYABLE, &[Copyable(2), Copyable(3)]);
    assert_eq!(OWNED, &[Owned(2), Owned(3)]);

    let [_, ref runtime @ .., _] = [Copyable(1), Copyable(2), Copyable(3), Copyable(4)];
    assert_eq!(COPYABLE, runtime);
}
