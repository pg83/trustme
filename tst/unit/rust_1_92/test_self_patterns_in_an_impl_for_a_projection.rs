// An impl may be written for an associated type: rkyv's derives implement
// their traits `for Archived<T>`, an alias of `<T as Archive>::Archived`, and
// match on `Self::Variant(..)` and `Self { .. }` in the methods. rustc
// normalizes the impl's self type, so `Self` names the enum or struct the
// projection stands for.
trait Archive {
    type Archived;
}

type Archived<T> = <T as Archive>::Archived;

enum Value {
    Number(u8),
    Empty,
}

enum ArchivedValue {
    Number(u8),
    Empty,
}

impl Archive for Value {
    type Archived = ArchivedValue;
}

struct Pair {
    left: u8,
    right: u8,
}

struct ArchivedPair {
    left: u8,
    right: u8,
}

impl Archive for Pair {
    type Archived = ArchivedPair;
}

trait Sum {
    fn sum(&self) -> u8;
}

impl Sum for Archived<Value> {
    fn sum(&self) -> u8 {
        match self {
            Self::Number(n) => *n,
            Self::Empty => 0,
        }
    }
}

impl Sum for Archived<Pair> {
    fn sum(&self) -> u8 {
        let Self { left, right } = self;
        left + right
    }
}

fn main() {
    let _ = (Value::Number(0), Value::Empty, Pair { left: 0, right: 0 });
    assert_eq!(ArchivedValue::Number(4).sum(), 4);
    assert_eq!(ArchivedValue::Empty.sum(), 0);
    assert_eq!(ArchivedPair { left: 2, right: 3 }.sum(), 5);
}
