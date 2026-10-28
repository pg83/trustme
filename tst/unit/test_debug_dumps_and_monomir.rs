//@ compile-flags: -Zdump-ast -Zdump-hir -Zdump-mir -Ccodegen-type=monomir -Cemit-cpp-only
//@ crate-type: lib

#[derive(Copy, Clone)]
struct Pair<T> {
    a: T,
    b: i32,
}

union Word {
    wide: u32,
    narrow: u8,
}

enum Choice<T> {
    Empty,
    One(T),
    Named { value: i32 },
}

trait Describe {
    const DEFAULT: i32;
    type Output;

    fn describe(&self, base: i32) -> i32 {
        base + Self::DEFAULT
    }
}

impl Describe for Pair<i32> {
    const DEFAULT: i32 = 2;
    type Output = i32;

    fn describe(&self, base: i32) -> i32 {
        self.a + self.b + base
    }
}

static STATIC_PAIR: Pair<i32> = Pair { a: 3, b: 4 };
const ANSWER: usize = 42;

macro_rules! add_one {
    ($value:expr) => {
        $value + 1
    };
}

fn helper(value: &mut i32) -> i32 {
    *value += 1;
    *value
}

fn main() {
    let mut pair = Pair { a: 1, b: 2 };
    let increment = |value: i32| value + 1;
    pair.a = increment(pair.a);
    pair.b = helper(&mut pair.b);

    for index in 0..3 {
        pair.a += index;
    }

    while pair.a < 10 {
        pair.a += 1;
    }

    loop {
        pair.b -= 1;
        if pair.b == 0 {
            break;
        }
    }

    let choice = if pair.a == 10 {
        Choice::Named { value: pair.a }
    } else {
        Choice::One(pair.a)
    };

    match choice {
        Choice::Empty => pair.a = 0,
        Choice::One(value) => pair.a = value,
        Choice::Named { value } => pair.a = value,
    }

    let values = [STATIC_PAIR.a, ANSWER as i32, add_one!(pair.a)];
    let _future = async move { values[0] + values[1] + values[2] };
    let _word = Word { wide: 7 };
}
