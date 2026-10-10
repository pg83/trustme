// konst's `iterator_shared!` builds the reversed iterator as
// `Type::<$crate::__choose!($is_forward $Rev $Self)> $fields`: a macro call
// in the generic arguments of a struct literal's path. Expression paths had
// their arguments expanded, struct literals' (and a destructuring
// assignment's) did not, and the unexpanded macro reached name resolution.
macro_rules! choose {
    (true $then:tt $($else:tt)?) => { $then };
    (false $then:tt $else:tt) => { $else };
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Forward {
    x: u32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Backward {
    x: u32,
}

type Type<T> = T;

impl Forward {
    pub const fn rev(self) -> choose!(true Backward Forward) {
        let Self { x } = self;
        Type::<choose!(true Backward Forward)> { x }
    }
}

fn main() {
    assert_eq!(Forward { x: 3 }.rev(), Backward { x: 3 });
    let x;
    Type::<choose!(false Backward Forward)> { x } = Forward { x: 5 };
    assert_eq!(x, 5);
}
